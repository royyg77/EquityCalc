#include "equitycalc/Parser.h"
#include "equitycalc/Combo.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace equitycalc {
namespace {
    
// ── Step A: character-level consumers ────────────────────────────────────────
// Contract: each consumer either fully advances pos on a match, or leaves pos
// exactly where it found it on a miss. No partial consumption on failure.

// Rank chars → 0-based rank index (0=2 … 12=A). Returns ~0u on non-rank.
static unsigned charToRank(char c) {
    switch (c) {
        case 'a': return 12; case 'k': return 11; case 'q': return 10;
        case 'j': return  9; case 't': return  8; case '9': return  7;
        case '8': return  6; case '7': return  5; case '6': return  4;
        case '5': return  3; case '4': return  2; case '3': return  1;
        case '2': return  0; default:  return ~0u;
    }
}

// Suit chars → OMP suit index (s=0,h=1,c=2,d=3). Returns ~0u on non-suit.
static unsigned charToSuit(char c) {
    switch (c) {
        case 's': return 0; case 'h': return 1;
        case 'c': return 2; case 'd': return 3;
        default:  return ~0u;
    }
}

// Try to consume exactly c. Advances pos on match.
static bool parseChar(std::string_view s, size_t& pos, char c) {
    if (pos < s.size() && s[pos] == c) { ++pos; return true; }
    return false;
}

// Try to consume a rank character. Advances pos on match.
static bool parseRank(std::string_view s, size_t& pos, unsigned& rank) {
    if (pos >= s.size()) return false;
    rank = charToRank(s[pos]);
    if (rank == ~0u) return false;
    ++pos;
    return true;
}

// Try to consume a suit character (s/h/c/d). Advances pos on match.
static bool parseSuit(std::string_view s, size_t& pos, unsigned& suit) {
    if (pos >= s.size()) return false;
    suit = charToSuit(s[pos]);
    if (suit == ~0u) return false;
    ++pos;
    return true;
}

// Consume a float weight. Throws on malformed or negative value.
// Safe: s is backed by a null-terminated std::string inside parseRange.
static float parseWeight(std::string_view s, size_t& pos) {
    if (pos >= s.size())
        throw ParseError("expected weight after ':'", pos);
    char* end = nullptr;
    float w = std::strtof(s.data() + pos, &end);
    size_t consumed = static_cast<size_t>(end - (s.data() + pos));
    if (consumed == 0)
        throw ParseError("invalid weight — expected a number", pos);
    if (w < 0.0f || std::isinf(w) || std::isnan(w))
        throw ParseError("weight must be a non-negative finite number", pos);
    pos += consumed;
    return w;
}

// ── Step B: expansion helpers (no cursor, no parsing) ────────────────────────

// Fix std::vector<Combo> type -> ComboVec
static void addCombo(ComboVec& out, unsigned c1, unsigned c2, float w) {
    out.emplace_back(Card(static_cast<uint8_t>(c1)),
                     Card(static_cast<uint8_t>(c2)), w);
}

// Expand rank1/rank2 into combos per suited/offsuited flags.
// Mirrors OMP's addCombos: the suited branch is skipped when r1==r2 (no suited pairs).
static void expandCombos(ComboVec& out,
                          unsigned r1, unsigned r2,
                          bool suited, bool offsuited, float w) {
    if (suited && r1 != r2) {
        for (unsigned suit = 0; suit < 4; ++suit)
            addCombo(out, 4*r1 + suit, 4*r2 + suit, w);
    }
    if (offsuited) {
        for (unsigned s1 = 0; s1 < 4; ++s1)
            for (unsigned s2 = s1 + 1; s2 < 4; ++s2) {
                addCombo(out, 4*r1 + s1, 4*r2 + s2, w);
                if (r1 != r2)
                    addCombo(out, 4*r1 + s2, 4*r2 + s1, w);
            }
    }
}

// All 1326 two-card combinations ("random").
static void expandAll(ComboVec& out, float w) {
    for (unsigned c1 = 1; c1 < 52; ++c1)
        for (unsigned c2 = 0; c2 < c1; ++c2)
            addCombo(out, c1, c2, w);
}

// ── Step C: parseHand ─────────────────────────────────────────────────────────

static ComboVec parseHand(std::string_view s, size_t& pos) {
    // "random" → all 1326 combos.
    if (pos + 6 <= s.size() && s.substr(pos, 6) == "random") {
        pos += 6;
        ComboVec out;
        expandAll(out, 1.0f);
        return out;
    }

    if (pos >= s.size())
        throw ParseError("expected hand", pos);

    // First rank is required to start any hand.
    unsigned r1 = 0;
    if (!parseRank(s, pos, r1))
        throw ParseError(
            std::string("unexpected character '") + s[pos] + "' — expected rank or 'random'",
            pos);

    // Try suit1: present → explicit-suits (specific) path; absent → generic path.
    unsigned s1 = 0;
    bool has_suit1 = parseSuit(s, pos, s1);

    // Second rank is required for any valid hand.
    unsigned r2 = 0;
    if (!parseRank(s, pos, r2))
        throw ParseError("expected second rank", pos);

    // ── Explicit-suits path: "AhKs" ──────────────────────────────────────────
    if (has_suit1) {
        unsigned s2 = 0;
        if (!parseSuit(s, pos, s2))
            throw ParseError("expected suit for second card in specific combo", pos);
        unsigned c1 = 4*r1 + s1, c2 = 4*r2 + s2;
        if (c1 == c2)
            throw ParseError("specific combo contains the same card twice", pos);
        ComboVec out;
        addCombo(out, c1, c2, 1.0f);
        return out;
    }

    // ── Generic path: rank rank [o|s]? [+|-...] ───────────────────────────────
    bool suited = true, offsuited = true;
    if      (parseChar(s, pos, 'o')) suited    = false;
    else if (parseChar(s, pos, 's')) offsuited = false;

    // "+" suffix — Hard Part #1: pair-plus and kicker-plus are TWO different loops.
    if (parseChar(s, pos, '+')) {
        ComboVec out;
        if (r1 == r2) {
            // Pair+: "QQ+" → QQ, KK, AA (this pair and every higher pair).
            for (unsigned r = r1; r <= 12; ++r)
                expandCombos(out, r, r, false, true, 1.0f);
        } else {
            // Kicker+: "KTs+" → KTs, KJs, KQs (second card climbs toward top card).
            unsigned hi = std::max(r1, r2), lo = std::min(r1, r2);
            for (unsigned k = lo; k < hi; ++k)
                expandCombos(out, hi, k, suited, offsuited, 1.0f);
        }
        return out;
    }

    // "-" suffix — dash range. Hard Part #2: direction-independent, both ends inclusive.
    if (parseChar(s, pos, '-')) {
        unsigned r3 = 0, r4 = 0;
        if (!parseRank(s, pos, r3))
            throw ParseError("expected rank after '-'", pos);
        if (!parseRank(s, pos, r4))
            throw ParseError("expected second rank after '-'", pos);

        bool suited2 = true, offsuited2 = true;
        if      (parseChar(s, pos, 'o')) suited2    = false;
        else if (parseChar(s, pos, 's')) offsuited2 = false;

        ComboVec out;
        if (r1 == r2) {
            // Pair range: "22-99" — endpoint must also be a pair.
            if (r3 != r4)
                throw ParseError(
                    "pair range endpoint must also be a pair (e.g. '22-99')", pos);
            unsigned lo_r = std::min(r1, r3), hi_r = std::max(r1, r3);
            for (unsigned r = lo_r; r <= hi_r; ++r)
                expandCombos(out, r, r, false, true, 1.0f);
        } else {
            // Kicker range: "A5s-A2s" — top card must match; kicker varies.
            unsigned hi1 = std::max(r1, r2), lo1 = std::min(r1, r2);
            unsigned hi2 = std::max(r3, r4), lo2 = std::min(r3, r4);
            if (hi1 != hi2)
                throw ParseError(
                    "kicker range must share the same top card on both sides"
                    " (e.g. 'A5s-A2s')", pos);
            if (suited != suited2 || offsuited != offsuited2)
                throw ParseError(
                    "kicker range endpoints must use the same suit modifier", pos);
            unsigned k_lo = std::min(lo1, lo2), k_hi = std::max(lo1, lo2);
            for (unsigned k = k_lo; k <= k_hi; ++k)
                expandCombos(out, hi1, k, suited, offsuited, 1.0f);
        }
        return out;
    }

    // Plain hand — no modifier.
    ComboVec out;
    if (r1 == r2)
        expandCombos(out, r1, r2, false, true, 1.0f);  // pairs are never suited
    else
        expandCombos(out, r1, r2, suited, offsuited, 1.0f);
    return out;
}

// ── Step D: parseElement ──────────────────────────────────────────────────────

// Parse one element: hand + optional ":weight". Advances pos.
// Weight is applied after expansion, to every combo produced by the hand.
static ComboVec parseElement(std::string_view s, size_t& pos) {
    ComboVec combos = parseHand(s, pos);

    float weight = 1.0f;
    if (parseChar(s, pos, ':'))
        weight = parseWeight(s, pos);

    if (weight != 1.0f)
        for (auto& c : combos) c.weight = weight;

    return combos;
}

} // anonymous namespace

// ── Step E: public entry points ───────────────────────────────────────────────

Range parseRange(std::string_view input) {
    // Normalize: strip all whitespace and control chars, toLower.
    std::string s;
    s.reserve(input.size());
    for (char c : input)
        if (std::isgraph(static_cast<unsigned char>(c)))
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    size_t pos = 0;
    Range range;

    do {
        for (auto& c : parseElement(s, pos))
            range.add(c);
    } while (parseChar(s, pos, ','));

    if (pos != s.size())
        throw ParseError(
            std::string("unexpected character '") + s[pos]
                + "' at position " + std::to_string(pos),
            pos);

    range.normalize();
    return range;
}

Card parseCard(std::string_view sv) {
    auto opt = Card::parse(sv);
    if (!opt)
        throw ParseError("invalid card: '" + std::string(sv) + "'", 0);
    return *opt;
}

} // namespace equitycalc

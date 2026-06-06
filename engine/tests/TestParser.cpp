#include "equitycalc/Parser.h"
#include <gtest/gtest.h>

using equitycalc::Card;
using equitycalc::Combo;
using equitycalc::ParseError;
using equitycalc::Range;
using equitycalc::parseCard;
using equitycalc::parseRange;

static Card C(const char* s) { return *Card::parse(s); }

// Returns true if the range contains a combo with the given two card strings.
static bool hasCombo(const Range& r, const char* a, const char* b) {
    Combo target(C(a), C(b));
    for (const auto& c : r.combos())
        if (c == target) return true;
    return false;
}

// ── parseCard ─────────────────────────────────────────────────────────────────

TEST(ParseCard, ValidCards) {
    EXPECT_EQ(parseCard("Ah"), C("Ah"));
    EXPECT_EQ(parseCard("2s"), C("2s"));
    EXPECT_EQ(parseCard("Kd"), C("Kd"));
}

TEST(ParseCard, InvalidCardThrows) {
    EXPECT_THROW(parseCard("Zz"), ParseError);
    EXPECT_THROW(parseCard(""),   ParseError);
    EXPECT_THROW(parseCard("A"),  ParseError);
}

// ── Specific: "AhKs" → 1 combo ───────────────────────────────────────────────

TEST(ParseRangeSpecific, OneCombo) {
    auto r = parseRange("AhKs");
    EXPECT_EQ(r.size(), 1u);
    EXPECT_TRUE(hasCombo(r, "Ah", "Ks"));
}

TEST(ParseRangeSpecific, ExactCards) {
    auto r = parseRange("AhKs");
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r.combos()[0].c1, C("Ah"));
    EXPECT_EQ(r.combos()[0].c2, C("Ks"));
}

TEST(ParseRangeSpecific, SameCardTwiceThrows) {
    EXPECT_THROW(parseRange("AhAh"), ParseError);
}

// ── Pair: "QQ" → 6 combos ────────────────────────────────────────────────────

TEST(ParseRangePair, SixCombos) {
    EXPECT_EQ(parseRange("QQ").size(), 6u);
}

TEST(ParseRangePair, AllSameRank) {
    for (const auto& c : parseRange("QQ").combos())
        EXPECT_EQ(c.c1.rank(), c.c2.rank()) << "every QQ combo must have two queens";
}

// ── Suited: "AKs" → 4 combos ─────────────────────────────────────────────────

TEST(ParseRangeSuited, FourCombos) {
    EXPECT_EQ(parseRange("AKs").size(), 4u);
}

TEST(ParseRangeSuited, AllSameSuit) {
    for (const auto& c : parseRange("AKs").combos())
        EXPECT_EQ(c.c1.suit(), c.c2.suit()) << "AKs combos must have matching suits";
}

// ── Offsuit: "AKo" → 12 combos ───────────────────────────────────────────────

TEST(ParseRangeOffsuit, TwelveCombos) {
    EXPECT_EQ(parseRange("AKo").size(), 12u);
}

TEST(ParseRangeOffsuit, AllDifferentSuits) {
    for (const auto& c : parseRange("AKo").combos())
        EXPECT_NE(c.c1.suit(), c.c2.suit()) << "AKo combos must have different suits";
}

// ── Anytwo: "AK" → 16 combos ─────────────────────────────────────────────────

TEST(ParseRangeAnytwo, SixteenCombos) {
    EXPECT_EQ(parseRange("AK").size(), 16u);
}

TEST(ParseRangeAnytwo, EqualsSuitedPlusOffsuit) {
    EXPECT_EQ(parseRange("AK").size(),
              parseRange("AKs").size() + parseRange("AKo").size());
}

// ── Pair-plus: "QQ+" → QQ,KK,AA = 18 combos ─────────────────────────────────

TEST(ParseRangePairPlus, EighteenCombos) {
    EXPECT_EQ(parseRange("QQ+").size(), 18u);
}

TEST(ParseRangePairPlus, ContainsQQKKAA) {
    auto r = parseRange("QQ+");
    EXPECT_TRUE(hasCombo(r, "Qh", "Qs")) << "QQ should be in QQ+";
    EXPECT_TRUE(hasCombo(r, "Kh", "Ks")) << "KK should be in QQ+";
    EXPECT_TRUE(hasCombo(r, "Ah", "As")) << "AA should be in QQ+";
}

TEST(ParseRangePairPlus, DoesNotContainJJ) {
    EXPECT_FALSE(hasCombo(parseRange("QQ+"), "Jh", "Js"))
        << "JJ is below QQ and must not appear in QQ+";
}

// ── Kicker-plus: "KTs+" → KTs,KJs,KQs = 12 combos ───────────────────────────
// Hard Part #1: kicker-plus and pair-plus are two different loops.

TEST(ParseRangeKickerPlus, TwelveCombos) {
    EXPECT_EQ(parseRange("KTs+").size(), 12u);
}

TEST(ParseRangeKickerPlus, AllSuited) {
    for (const auto& c : parseRange("KTs+").combos())
        EXPECT_EQ(c.c1.suit(), c.c2.suit()) << "KTs+ combos must all be suited";
}

TEST(ParseRangeKickerPlus, NoPairsPresent) {
    // The key invariant: kicker-plus must never emit a pair.
    for (const auto& c : parseRange("KTs+").combos())
        EXPECT_NE(c.c1.rank(), c.c2.rank()) << "KTs+ must not contain any pair";
}

TEST(ParseRangeKickerPlus, OneCardAlwaysKing) {
    for (const auto& c : parseRange("KTs+").combos()) {
        bool has_king = (c.c1.rank() == 11) || (c.c2.rank() == 11);
        EXPECT_TRUE(has_king) << "every KTs+ combo must contain a King";
    }
}

TEST(ParseRangeKickerPlus, KickerStaysBelowTopCard) {
    // Kicker must be in {T(8), J(9), Q(10)} — never reaches K(11).
    for (const auto& c : parseRange("KTs+").combos()) {
        unsigned kicker = (c.c1.rank() == 11) ? c.c2.rank() : c.c1.rank();
        EXPECT_GE(kicker, 8u) << "kicker must be at least T(8)";
        EXPECT_LE(kicker, 10u) << "kicker must not reach K(11)";
    }
}

// ── Pair range: "22-99" → 8 pairs (48 combos) ────────────────────────────────
// Hard Part #2: direction-independent; both ends inclusive.

TEST(ParseRangePairRange, FortyEightCombos) {
    EXPECT_EQ(parseRange("22-99").size(), 48u);
}

TEST(ParseRangePairRange, ReversedEndpointsIdentical) {
    auto fwd = parseRange("22-99").combos();
    auto rev = parseRange("99-22").combos();
    ASSERT_EQ(fwd.size(), rev.size());
    for (size_t i = 0; i < fwd.size(); ++i)
        EXPECT_EQ(fwd[i], rev[i]) << "99-22 must normalize to the same range as 22-99";
}

TEST(ParseRangePairRange, AllCombosArePairs) {
    for (const auto& c : parseRange("22-99").combos())
        EXPECT_EQ(c.c1.rank(), c.c2.rank()) << "every 22-99 combo must be a pair";
}

TEST(ParseRangePairRange, RanksBounded) {
    for (const auto& c : parseRange("22-99").combos()) {
        EXPECT_GE(c.c1.rank(), 0u) << "rank must be at least 2 (rank index 0)";
        EXPECT_LE(c.c1.rank(), 7u) << "rank must be at most 9 (rank index 7)";
    }
}

// ── Kicker range: "A5s-A2s" → A5s,A4s,A3s,A2s = 16 combos ──────────────────

TEST(ParseRangeKickerRange, SixteenCombos) {
    EXPECT_EQ(parseRange("A5s-A2s").size(), 16u);
}

TEST(ParseRangeKickerRange, AllSuited) {
    for (const auto& c : parseRange("A5s-A2s").combos())
        EXPECT_EQ(c.c1.suit(), c.c2.suit()) << "A5s-A2s combos must all be suited";
}

TEST(ParseRangeKickerRange, OneCardAlwaysAce) {
    for (const auto& c : parseRange("A5s-A2s").combos()) {
        bool has_ace = (c.c1.rank() == 12) || (c.c2.rank() == 12);
        EXPECT_TRUE(has_ace) << "every A5s-A2s combo must contain an Ace";
    }
}

TEST(ParseRangeKickerRange, ReversedEndpointsIdentical) {
    auto fwd = parseRange("A5s-A2s").combos();
    auto rev = parseRange("A2s-A5s").combos();
    ASSERT_EQ(fwd.size(), rev.size());
    for (size_t i = 0; i < fwd.size(); ++i)
        EXPECT_EQ(fwd[i], rev[i]) << "A2s-A5s must normalize to the same range as A5s-A2s";
}

// ── Combination: "QQ+,AKs" → union, deduped ──────────────────────────────────

TEST(ParseRangeCombination, UnionSize) {
    // QQ+(18) + AKs(4), no overlap → 22
    EXPECT_EQ(parseRange("QQ+,AKs").size(), 22u);
}

TEST(ParseRangeCombination, DedupedAfterNormalize) {
    // "QQ+,QQ" — QQ is already included in QQ+; dedup leaves 18 total
    EXPECT_EQ(parseRange("QQ+,QQ").size(), 18u);
}

// ── Weights ───────────────────────────────────────────────────────────────────
// Hard Part #4: weights attach to the element after expansion.

TEST(ParseRangeWeight, AllCombosWeighted) {
    auto r = parseRange("QQ:0.5");
    EXPECT_EQ(r.size(), 6u);
    for (const auto& c : r.combos())
        EXPECT_FLOAT_EQ(c.weight, 0.5f) << "all QQ combos must have weight 0.5";
}

TEST(ParseRangeWeight, PerElementWeight) {
    // "QQ:0.5,KK" → QQ at 0.5, KK at default 1.0
    auto r = parseRange("QQ:0.5,KK");
    EXPECT_EQ(r.size(), 12u);
    for (const auto& c : r.combos()) {
        if (c.c1.rank() == 10) // Q rank
            EXPECT_FLOAT_EQ(c.weight, 0.5f) << "QQ combos must be weighted 0.5";
        else
            EXPECT_FLOAT_EQ(c.weight, 1.0f) << "KK combos must be weighted 1.0";
    }
}

TEST(ParseRangeWeight, PairPlusWeightAppliesToAllExpanded) {
    // "QQ+:0.5" → all 18 combos (QQ, KK, AA) get weight 0.5
    auto r = parseRange("QQ+:0.5");
    EXPECT_EQ(r.size(), 18u);
    for (const auto& c : r.combos())
        EXPECT_FLOAT_EQ(c.weight, 0.5f) << "QQ+:0.5 must weight all 18 combos at 0.5";
}

// ── "random" → 1326 combos ───────────────────────────────────────────────────

TEST(ParseRangeRandom, AllCombos) {
    EXPECT_EQ(parseRange("random").size(), 1326u);
}

// ── Case / whitespace insensitivity ──────────────────────────────────────────

TEST(ParseRangeCaseWhitespace, CaseInsensitive) {
    EXPECT_EQ(parseRange("QQ+").size(),  parseRange("qq+").size());
    EXPECT_EQ(parseRange("AKs").size(),  parseRange("aks").size());
    EXPECT_EQ(parseRange("AhKs").size(), parseRange("ahks").size());
}

TEST(ParseRangeCaseWhitespace, InternalAndSurroundingWhitespace) {
    auto canonical = parseRange("QQ+,AKs");
    auto spaced    = parseRange("  qq+ , aks ");
    ASSERT_EQ(canonical.size(), spaced.size());
    for (size_t i = 0; i < canonical.size(); ++i)
        EXPECT_EQ(canonical.combos()[i], spaced.combos()[i]);
}

// ── Error handling (strict) ───────────────────────────────────────────────────

TEST(ParseRangeErrors, BadSecondCardThrows) {
    // "AhZz" → 'z' is not a valid rank after the suit
    EXPECT_THROW(parseRange("AhZz"), ParseError);
}

TEST(ParseRangeErrors, BadSecondCardPosition) {
    try {
        parseRange("AhZz");
        FAIL() << "expected ParseError";
    } catch (const ParseError& e) {
        EXPECT_EQ(e.position, 2u) << "error should be at position 2 (the 'z' after 'Ah')";
    }
}

TEST(ParseRangeErrors, DoublePlusThrows) {
    // "QQ++" → trailing '+' is unexpected garbage
    EXPECT_THROW(parseRange("QQ++"), ParseError);
}

TEST(ParseRangeErrors, DanglingDashThrows) {
    // "QQ-" → '-' with no second endpoint
    EXPECT_THROW(parseRange("QQ-"), ParseError);
}

TEST(ParseRangeErrors, BadTokenThrows) {
    // "5x" → 'x' is not a valid second rank
    EXPECT_THROW(parseRange("5x"), ParseError);
}

TEST(ParseRangeErrors, TrailingGarbageThrows) {
    // "AhKs!" → '!' is unexpected after a valid combo
    EXPECT_THROW(parseRange("AhKs!"), ParseError);
}

TEST(ParseRangeErrors, NearMissReportsCorrectPosition) {
    // "AhXX" — error must be reported at position 2 (the 'x'), not at start
    try {
        parseRange("AhXX");
        FAIL() << "expected ParseError";
    } catch (const ParseError& e) {
        EXPECT_EQ(e.position, 2u) << "cursor must not regress before Ah when reporting the error";
    }
}

TEST(ParseRangeErrors, MismatchedKickerRangeTopCardThrows) {
    // "A5s-K2s" → top cards differ (A vs K)
    EXPECT_THROW(parseRange("A5s-K2s"), ParseError);
}

TEST(ParseRangeErrors, PairRangeNonPairEndpointThrows) {
    // "22-AK" → second endpoint is not a pair
    EXPECT_THROW(parseRange("22-AK"), ParseError);
}

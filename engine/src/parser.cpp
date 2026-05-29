#include "equitycalc/parser.h"
#include <cctype>
#include <stdexcept>

// TODO: implement full recursive-descent parser per design-docs/05-parser.md.
//
// Internal structure (all private to this TU):
//   parseElement(s, cursor)  → vector<Combo> + weight
//   parseHand(s, cursor)     → vector<Combo>
//   parseRank(s, cursor)     → uint8_t
//   parseSuit(s, cursor)     → uint8_t
//   parseWeight(s, cursor)   → float
//   parseChar(s, cursor, c)  → bool
//
// Expansion helpers:
//   expandSpecific, expandPair, expandSuited, expandOffsuit, expandAnytwo
//   expandPairPlus, expandKickerPlus, expandPairRange, expandKickerRange
//
// Key correctness notes (from design doc):
//   - '+' for pairs: "QQ+" → higher pairs (QQ, KK, AA).
//   - '+' for non-pairs: "KTs+" → kicker climbs (KTs, KJs, KQs). Different loop.
//   - Dash ranges accept either direction; normalize internally.
//   - Weights attach to the whole element after expansion.
//   - Strict: any unparseable token throws ParseError with position.
//   - Cursor is always fully restored on failure (no partial consumption).

namespace equitycalc {

Range parseRange(std::string_view /*input*/) {
    throw std::runtime_error("parseRange: not yet implemented");
}

Card parseCard(std::string_view s) {
    auto opt = Card::parse(s);
    if (!opt) {
        throw ParseError("invalid card: '" + std::string(s) + "'", 0);
    }
    return *opt;
}

} // namespace equitycalc

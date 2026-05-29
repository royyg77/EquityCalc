#pragma once

#include "equitycalc/range.h"
#include <stdexcept>
#include <string>
#include <string_view>

namespace equitycalc {

// Structured parse error with position information.
struct ParseError : std::runtime_error {
    size_t position; // byte offset into the input where the error occurred

    ParseError(std::string msg, size_t pos)
        : std::runtime_error(std::move(msg)), position(pos) {}
};

// Free-function parsing module. Not a class.
// Grammar (abbreviated):
//   range   := element ("," element)*
//   element := hand (":" float)?
//   hand    := specific | pair | suited | offsuit | anytwo
//             | pair_plus | suited_plus | offsuit_plus
//             | pair_range | suited_range | offsuit_range | "random"
//
// Rules:
//   "QQ+"   → pair-plus: QQ, KK, AA (higher pairs)
//   "KTs+"  → kicker-plus: KTs, KJs, KQs (kicker climbs toward top card)
//   "22-99" → pair range (both directions accepted)
//   "A5s-A2s" → suited kicker range
//   Weights apply to the whole element: "QQ+:0.5" weights all of QQ, KK, AA.
//   Case-insensitive. Strict: any unparseable token raises ParseError.
//
// Returns a normalized (sorted, deduplicated) Range on success.
// Throws ParseError on malformed input.
Range parseRange(std::string_view input);

// Parse a single card string ("Ah", "2s", …). Convenience wrapper.
// Throws ParseError if the string is not a valid card.
Card parseCard(std::string_view s);

}  // namespace equitycalc

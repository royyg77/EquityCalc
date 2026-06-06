#pragma once

#include "equitycalc/Range.h"
#include <stdexcept>
#include <string>
#include <string_view>

namespace equitycalc {

// Structured parse error.
// position is a 0-based byte offset into the normalized input (lowercased,
// whitespace stripped) — the same string the parser walks internally.
struct ParseError : std::runtime_error {
    size_t position;

    ParseError(std::string msg, size_t pos)
        : std::runtime_error(std::move(msg)), position(pos) {}
};

// Free-function parsing module — not a class.
//
// ── Grammar ──────────────────────────────────────────────────────────────────
//   range         := element ("," element)*
//   element       := hand (":" weight)?
//   weight        := float                       ; [0, ∞)
//
//   hand          := specific
//                  | pair | suited | offsuit | anytwo
//                  | pair_plus | suited_plus | offsuit_plus
//                  | pair_range | suited_range | offsuit_range
//                  | "random"
//
//   specific      := rank suit rank suit         ; "AhKs"        →  1 combo
//   pair          := rank rank      (equal)      ; "QQ"          →  6 combos
//   suited        := rank rank 's'               ; "AKs"         →  4 combos
//   offsuit       := rank rank 'o'               ; "AKo"         → 12 combos
//   anytwo        := rank rank      (unequal)    ; "AK"          → 16 combos
//   pair_plus     := pair '+'                    ; "QQ+"  → QQ,KK,AA (18 combos)
//   suited_plus   := suited '+'                  ; "KTs+" → KTs,KJs,KQs (12 combos)
//   offsuit_plus  := offsuit '+'                 ; same kicker-climb semantics
//   pair_range    := pair '-' pair               ; "22-99" → 8 pairs, either direction
//   suited_range  := suited '-' suited           ; "A5s-A2s" → A5s,A4s,A3s,A2s (16)
//   offsuit_range := offsuit '-' offsuit
//   random        := "random"                    ; all 1326 combos
//
// ── Semantics ─────────────────────────────────────────────────────────────────
//   Kicker-plus ("KTs+"): top card is fixed; second card climbs toward but not
//   reaching the top card. "A5s-K2s" is an error (top card must match).
//   Weights attach to the whole element: "QQ+:0.5" weights all of QQ, KK, AA.
//   Strict: any unparseable token throws ParseError; input is never silently ignored.
//
// Returns a normalized (sorted, deduplicated) Range on success.
// Throws ParseError on malformed input.
Range parseRange(std::string_view input);

// Parse a single card string ("Ah", "2s", …). Convenience wrapper.
// Throws ParseError if the string is not a valid card.
Card parseCard(std::string_view s);

}  // namespace equitycalc
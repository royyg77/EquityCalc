#pragma once

// OMP boundary: this header includes <omp/Hand.h>.
// Requires OMPEval vendored at engine/third_party/OMPEval/ with that path on
// the include search path.

#include "equitycalc/card.h"
#include <omp/Hand.h>
#include <cstdint>

namespace equitycalc {

// Two specific hole cards plus a weight and the cached evaluation data.
//
// The omp::Hand (evalHand) is built ONCE in the constructor and reused for
// every board in every matchup. The inner-loop combine is:
//   omp::Hand seven = combo.evalHand + board.evalHand;  // one SSE add
// Do NOT rebuild omp::Hand from card indices inside the loop.
class Combo {
public:
    Card    c1;        // higher card (canonical: c1 >= c2 by value)
    Card    c2;        // lower card
    float   weight;    // default 1.0; range [0, ∞) — parser ensures sanity
    uint64_t cardMask; // exactly 2 bits set: bit c1.value and bit c2.value
    omp::Hand evalHand; // pre-built hand; combined with board in the hot loop

    // Canonicalizes order (higher card first), builds cardMask and evalHand.
    // Asserts c1 != c2 (same card twice is illegal).
    Combo(Card a, Card b, float w = 1.0f);

    // Conflict checks — O(1) bitwise AND.
    bool conflicts(const Combo& other) const noexcept {
        return (cardMask & other.cardMask) != 0;
    }
    bool conflicts(uint64_t usedMask) const noexcept {
        return (cardMask & usedMask) != 0;
    }

    // Equality compares cards only; weight is intentionally ignored.
    // Two combos with the same cards are the same combo regardless of weight.
    bool operator==(const Combo& o) const noexcept {
        return c1 == o.c1 && c2 == o.c2;
    }
    bool operator!=(const Combo& o) const noexcept { return !(*this == o); }
    bool operator<(const Combo& o) const noexcept {
        if (c1 != o.c1) return c1 < o.c1;
        return c2 < o.c2;
    }
};

}  // namespace equitycalc

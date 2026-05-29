#include "equitycalc/combo.h"
#include <cassert>

namespace equitycalc {

Combo::Combo(Card a, Card b, float w)
    : c1(a), c2(b), weight(w)
{
    assert(a.value != b.value && "Combo: cannot hold the same card twice");
    assert(w >= 0.0f && "Combo: weight must be non-negative");

    // Canonical order: higher card (by value) goes in c1.
    if (c1 < c2) {
        Card tmp = c1; c1 = c2; c2 = tmp;
    }

    cardMask = (uint64_t{1} << c1.value) | (uint64_t{1} << c2.value);

    // Build evalHand once here; reused for every board in the hot loop.
    evalHand = omp::Hand(c1.value) + omp::Hand(c2.value);
}

} // namespace equitycalc

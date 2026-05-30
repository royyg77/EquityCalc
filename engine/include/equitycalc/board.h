#pragma once

// OMP boundary: this header includes <omp/Hand.h>.
// Requires OMPEval vendored at engine/third_party/OMPEval/.

#include "equitycalc/card.h"
#include "equitycalc/combo.h"
#include <omp/Hand.h>
#include <cstdint>
#include <string>

namespace equitycalc {

// Community cards (0, 3, 4, or 5).
//
// Stores an omp::Hand accumulator (not an array of Cards) so there are no
// undealt slots and no sentinel values. evalHand MUST be initialized from
// omp::Hand::empty() — never default-constructed (which leaves it uninitialized
// and produces silently wrong flush detection and rank keys).
class Board {
public:
    // Empty board: evalHand = omp::Hand::empty(), cardMask = 0.
    Board();

    // Build a fixed board from a bitmask (e.g. from --board argument).
    static Board fromMask(uint64_t mask);

    // Add a card in-place. Asserts the card is not already present.
    void addCard(Card c);

    // Non-mutating variant — returns a new Board with the card added.
    // Use this during enumeration so recursion branches don't stomp each other.
    Board withCard(Card c) const;

    unsigned  count() const noexcept { return evalHand_.count(); }
    uint64_t  mask()  const noexcept { return cardMask_; }

    // The accumulated omp::Hand — combined with combo.evalHand in the hot loop.
    const omp::Hand& hand() const noexcept { return evalHand_; }

    bool conflicts(const Combo& c) const noexcept {
        return (cardMask_ & c.cardMask) != 0;
    }

    // Reconstruct cards from cardMask for display. Not called in the hot loop.
    std::string toString() const;

private:
    omp::Hand evalHand_; // initialized from omp::Hand::empty()
    uint64_t  cardMask_{0};
};

}  // namespace equitycalc

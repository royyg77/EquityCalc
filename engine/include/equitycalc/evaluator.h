#pragma once

// OMP boundary: this header includes <omp/HandEvaluator.h>.
// Requires OMPEval vendored at engine/third_party/OMPEval/.

#include "equitycalc/combo.h"
#include "equitycalc/board.h"
#include <omp/HandEvaluator.h>
#include <cstdint>

namespace equitycalc {

// Hand category decoded from a strength value (strength >> 12 gives 1–9).
enum class HandCategory : uint8_t {
    HighCard      = 1,
    OnePair       = 2,
    TwoPair       = 3,
    Trips         = 4,
    Straight      = 5,
    Flush         = 6,
    FullHouse     = 7,
    Quads         = 8,
    StraightFlush = 9,
};

// Thin wrapper around omp::HandEvaluator.
//
// Construct ONE instance and share it across all threads — evaluate() is const
// and the lookup tables are static, so concurrent calls are safe. Do not
// construct one per thread or per evaluation.
class Evaluator {
public:
    // Constructor triggers OMP's one-time static table initialization.
    // First construction pays the cost; subsequent ones are cheap.
    Evaluator();

    // Score a fully-built 5–7 card omp::Hand. Higher = stronger.
    // The hand is typically: combo.evalHand + board.hand() (one SSE add by caller).
    uint16_t evaluate(const omp::Hand& hand) const;

    // Convenience: combine combo + board, then evaluate.
    uint16_t evaluate(const Combo& c, const Board& b) const;

    // Decode hand category from a strength value (strength >> 12).
    static HandCategory category(uint16_t strength) noexcept {
        return static_cast<HandCategory>(strength >> 12);
    }

private:
    omp::HandEvaluator eval_;
};

}  // namespace equitycalc

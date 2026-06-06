#include "equitycalc/Evaluator.h"

namespace equitycalc {

Evaluator::Evaluator() {
    // Constructing eval_ triggers OMP's one-time static table initialization
    // (guarded; safe to construct from multiple threads). Subsequent constructions
    // are cheap. Do not construct inside loops or per-thread.
}

uint16_t Evaluator::evaluate(const omp::Hand& hand) const {
    return eval_.evaluate(hand);
}

uint16_t Evaluator::evaluate(const Combo& c, const Board& b) const {
    return eval_.evaluate(c.evalHand + b.hand());
}

} // namespace equitycalc

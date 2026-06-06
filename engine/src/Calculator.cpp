#include "equitycalc/Calculator.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <stdexcept>

// TODO: implement per design-docs/07-calculator.md.
//
// Implementation notes (build order from the design doc):
//   Step 1: 2-player hand vs hand, fixed board (single-threaded, enumerate)
//   Step 2: hand vs range (outer loop over one range)
//   Step 3: range vs range (cartesian product + inline conflict skip)
//   Step 4: partial board enumeration (recursive board completion)
//   Step 5: N-way tally (winsByPlayerMask trick — single add per scenario)
//   Step 6: threading (per-thread local tally, merge at the end)
//   Step 7: Monte Carlo (rejection sampling + convergence check)
//
// Hot-loop shape (from design doc):
//   for c0 in range[0].combos:
//     for c1 in range[1].combos:
//       if c0.cardMask & c1.cardMask: continue   // conflict skip (~1ns)
//       ...
//         usedMask = board_.mask() | deadMask_ | c0.cardMask | c1.cardMask | ...
//         enumerate board completions:
//           omp::Hand seven = combo.evalHand + board.hand()  // SSE add
//           tally[winnersMask] += weight                     // single add
//
// DO NOT build CombinedRange for v1.0 — that is a v1.5 optimization.

namespace equitycalc {

Calculator::Calculator(std::vector<Range> ranges, Board board,
                       uint64_t deadMask, CalculatorConfig cfg)
    : ranges_(std::move(ranges))
    , board_(board)
    , deadMask_(deadMask)
    , cfg_(cfg)
{
    assert(ranges_.size() >= 2 && ranges_.size() <= MAX_PLAYERS);
}

Result Calculator::compute() {
    // Remove combos that conflict with board + dead cards.
    uint64_t fixedMask = board_.mask() | deadMask_;
    for (auto& r : ranges_) r.removeConflicts(fixedMask);

    uint64_t total = preflopCombinationCount() * postflopCombinationCount();
    if (total < cfg_.enumThreshold) return computeExact();
    return computeMonteCarlo();
}

Result Calculator::computeExact() {
    // TODO: implement exact enumeration.
    // See design-docs/07-calculator.md §4 (cartesian product) and §5 (board enum).
    throw std::runtime_error("computeExact: not yet implemented");
}

Result Calculator::computeMonteCarlo() {
    // TODO: implement Monte Carlo sampling.
    // See design-docs/07-calculator.md §8 (convergence) and §7 (threading).
    throw std::runtime_error("computeMonteCarlo: not yet implemented");
}

uint64_t Calculator::preflopCombinationCount() const {
    uint64_t product = 1;
    for (const auto& r : ranges_) product *= r.size();
    return product;
}

uint64_t Calculator::postflopCombinationCount() const {
    // C(n, k) where n = cards remaining in deck, k = board cards still to come.
    // TODO: count cards used by board + deadCards_ + (pre-conflict estimate for ranges).
    unsigned boardCount = board_.count();
    unsigned k = (boardCount >= 5) ? 0 : (5 - boardCount);
    if (k == 0) return 1;

    // Approximate deck size (conflict removal makes this an estimate).
    unsigned n = 52 - static_cast<unsigned>(__builtin_popcountll(board_.mask() | deadMask_));
    uint64_t result = 1;
    for (unsigned i = 0; i < k; ++i) {
        result = result * (n - i) / (i + 1);
    }
    return result;
}

void Calculator::evaluateShowdown(const omp::Hand* hands, unsigned n,
                                   const Board& board, double weight,
                                   double* tally) const {
    uint16_t bestRank = 0;
    unsigned winnersMask = 0;

    for (unsigned i = 0; i < n; ++i) {
        uint16_t rank = eval_.evaluate(hands[i] + board.hand());
        if (rank > bestRank) {
            bestRank = rank;
            winnersMask = (1u << i);
        } else if (rank == bestRank) {
            winnersMask |= (1u << i);
        }
    }

    tally[winnersMask] += weight;
}

Result Calculator::buildResult(const double* tally, uint64_t scenarios,
                                double totalWeight, double timeSeconds) const {
    unsigned n = static_cast<unsigned>(ranges_.size());
    Result r;
    r.players         = n;
    r.totalScenarios  = scenarios;
    r.totalWeightTallied = totalWeight;
    r.timeSeconds     = timeSeconds;

    double wins[MAX_PLAYERS]{};
    double ties[MAX_PLAYERS]{};

    unsigned maskCount = 1u << n;
    for (unsigned mask = 1; mask < maskCount; ++mask) {
        double w = tally[mask];
        if (w == 0.0) continue;
        r.winsByPlayerMask[mask] = w;
        int cnt = __builtin_popcount(mask);
        for (unsigned i = 0; i < n; ++i) {
            if (mask & (1u << i)) {
                if (cnt == 1) wins[i] += w;
                else          ties[i] += w / cnt;
            }
        }
    }

    for (unsigned i = 0; i < n; ++i) {
        r.wins[i]   = wins[i];
        r.ties[i]   = ties[i];
        r.equity[i] = (totalWeight > 0.0) ? (wins[i] + ties[i]) / totalWeight : 0.0;
    }
    return r;
}

} // namespace equitycalc

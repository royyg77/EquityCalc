#pragma once

#include "equitycalc/Board.h"
#include "equitycalc/Evaluator.h"
#include "equitycalc/Range.h"
#include "equitycalc/Result.h"
#include <cstdint>
#include <vector>

namespace equitycalc {

struct CalculatorConfig {
    unsigned threadCount{1};
    double   mcStdevTarget{5e-5};   // MC stops when equity std-dev drops below this
    uint64_t enumThreshold{50'000'000}; // scenario count; above this → MC
};

// Orchestrates the equity calculation.
//
// Mode selection: totalScenarios = preflopCombos * postflopBoards.
// If totalScenarios < enumThreshold → exact enumeration; otherwise → Monte Carlo.
// Player count is not used directly — it falls out of the scenario product.
//
// One Evaluator is shared across all threads (evaluate() is const/thread-safe).
class Calculator {
public:
    explicit Calculator(std::vector<Range> ranges,
                        Board              board    = Board{},
                        uint64_t           deadMask = 0,
                        CalculatorConfig   cfg      = {});

    // Entry point: remove conflicts, select mode, run, return Result.
    Result compute();

private:
    Result computeExact();
    Result computeMonteCarlo();

    // Number of combo combinations across all ranges (pre-conflict estimate).
    uint64_t preflopCombinationCount() const;

    // C(deck_remaining, board_cards_to_come).
    uint64_t postflopCombinationCount() const;

    // Score one fully-dealt scenario and accumulate into tally.
    // hands[i] = combo.evalHand for player i. weight = product of combo weights.
    void evaluateShowdown(const omp::Hand* hands, unsigned n,
                          const Board& board, double weight,
                          double* tally) const;

    // Build a Result from a completed tally array.
    Result buildResult(const double* tally, uint64_t scenarios,
                       double totalWeight, double timeSeconds) const;

    std::vector<Range> ranges_;
    Board              board_;
    uint64_t           deadMask_;
    CalculatorConfig   cfg_;
    Evaluator          eval_; // single shared instance
};

}  // namespace equitycalc

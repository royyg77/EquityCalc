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
    double   mcCiTarget{2e-4};       // MC stops when max per-player CI half-width < this
    uint64_t enumThreshold{50'000'000}; // estimated total scenarios; above → MC
    uint64_t mcSeed{0};
};

// Orchestrates equity calculation.
//
// Mode selection: totalScenarios ≈ N_matchups * runoutsPerMatchup.
// If totalScenarios < enumThreshold → exact enumeration; otherwise → Monte Carlo.
// Player count is not consulted directly — it falls out of the scenario product.
//
// One Evaluator is constructed in the Calculator and shared across all worker
// threads (evaluate() is const / thread-safe after one-time static init).
class Calculator {
public:
    explicit Calculator(std::vector<Range> ranges,
                        Board              board    = Board{},
                        uint64_t           deadMask = 0,
                        CalculatorConfig   cfg      = {});

    // Run setup, select mode, execute driver, return Result.
    Result compute();

private:
    // ── Accumulator ───────────────────────────────────────────────────────────
    // All scored scenarios write into one of these. Fields travel together so
    // the denominator cannot drift from the tally (Decision 3 in design doc).
    // merge() is elementwise addition — result is deterministic regardless of
    // how work is partitioned across threads.
    struct BatchAccumulator {
        double   winsByPlayerMask[1u << MAX_PLAYERS] = {};
        double   totalWeightTallied = 0.0;
        uint64_t scenariosScored    = 0;

        void merge(const BatchAccumulator& o) noexcept {
            for (unsigned m = 0; m < (1u << MAX_PLAYERS); ++m)
                winsByPlayerMask[m] += o.winsByPlayerMask[m];
            totalWeightTallied += o.totalWeightTallied;
            scenariosScored    += o.scenariosScored;
        }
    };

    // ── Drivers ───────────────────────────────────────────────────────────────
    Result computeExact();
    Result computeMonteCarlo();

    // ── Inner-loop helpers ────────────────────────────────────────────────────

    // Decode flat matchup index p → comboIdx[] via mixed-radix (Decision 1).
    // libdivide seam: swap rangeSizes_ type + plain %/÷ for libdivide_u64 in v1.5.
    void decodeMatchup(uint64_t p, unsigned* comboIdx) const noexcept;

    // Build available-card deck once per matchup, then recurse all runouts
    // (Decision 2: deck built here, not rebuilt inside recurseBoard).
    void runOutBoard(const omp::Hand* playerHands, unsigned n,
                     uint64_t usedMask, double weight,
                     BatchAccumulator& acc) const;

    // Hot inner loop: recurse combinations of remaining board cards.
    // start = i+1 gives C(ndeck, remaining) combinations — each runout once.
    // v1.2 seam: suitCounts[] suit-isomorphism pruning threads through here.
    void recurseBoard(const omp::Hand* playerHands, unsigned n,
                      const omp::Hand& boardHand,
                      const uint8_t* deck, unsigned ndeck,
                      unsigned start, unsigned remaining,
                      double weight, BatchAccumulator& acc) const;

    // Convergence point (Decision 4): score one fully-dealt scenario.
    // Numerator and denominator updated HERE and only here — no caller can tally
    // a scenario without its weight entering totalWeightTallied.
    void scoreScenario(const omp::Hand* playerHands, unsigned n,
                       const omp::Hand& board, double weight,
                       BatchAccumulator& acc) const noexcept;

    // Build per-range cumulative-weight arrays for MC sampling.
    // Must run after removeConflicts so cumulative_.back() == range.totalWeight().
    void buildSamplers();

    // Binary search on cumulative_[rangeIdx] — returns combo index whose
    // bracket contains u (u in [0, cumulative_.back())).
    unsigned drawCombo(unsigned rangeIdx, double u) const;

    // Derive Result from the completed accumulator (runs once after the driver).
    Result buildResult(const BatchAccumulator& acc, unsigned n,
                       double timeSeconds, Mode mode) const;

    // 95% CI half-width = max over players of 1.96*sqrt(eq*(1-eq)/N).
    // Called at MC batch boundaries to test convergence.
    static double computeCiHalfWidth(const BatchAccumulator& acc,
                                     unsigned n) noexcept;

    // ── Data members ──────────────────────────────────────────────────────────
    std::vector<Range>  ranges_;
    Board               board_;
    uint64_t            deadMask_;
    CalculatorConfig    cfg_;
    Evaluator           eval_;   // single shared instance — evaluate() is const

    // Mixed-radix bases for decodeMatchup (Decision 1).
    // libdivide seam: change type to libdivide::libdivide_u64_t in v1.5.
    uint64_t            rangeSizes_[MAX_PLAYERS] = {};
    uint64_t            N_ = 0;  // ∏ rangeSizes_ — total matchup count

    // MC only — per-range cumulative weight arrays built by buildSamplers().
    std::vector<double> cumulative_[MAX_PLAYERS];
};

}  // namespace equitycalc

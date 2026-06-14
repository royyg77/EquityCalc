#include "equitycalc/Calculator.h"
#include "equitycalc/Board.h"
#include "equitycalc/Parser.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace equitycalc;

// ── Shared helpers ────────────────────────────────────────────────────────────

namespace {

// Build a 52-bit card mask from a concatenated card string ("Ah7d2c").
// Uses parseCard so the encoding contract is exercised on every board.
uint64_t maskOf(std::string_view cards) {
    uint64_t m = 0;
    for (size_t i = 0; i + 1 < cards.size(); i += 2)
        m |= uint64_t{1} << parseCard(cards.substr(i, 2)).value;
    return m;
}

// Build vector<Range> from string literals.
std::vector<Range> ranges(std::initializer_list<std::string_view> rs) {
    std::vector<Range> v;
    v.reserve(rs.size());
    for (auto r : rs) v.push_back(parseRange(r));
    return v;
}

} // namespace

// ── Category 1: Known-equity anchors ─────────────────────────────────────────
// These are the primary canaries for the encoding/evaluator contract.
// All are forced exact (enumThreshold = UINT64_MAX) so there is zero sampling
// variance and the assertion is on a deterministic value.

// Board 2h7s9dTcJh: P1 (AhKs) has A-high; P2 (QdQc) has a pair of queens.
// Pair beats ace-high → P2 wins outright. Both equities pinned to 0.0/1.0.
TEST(CalculatorIntegration, CompleteBoardDeterministicWinner) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"AhKs", "QdQc"}),
                    Board::fromMask(maskOf("2h7s9dTcJh")),
                    0, cfg);
    Result r = calc.compute();

    EXPECT_EQ(r.players, 2u);
    EXPECT_EQ(r.mode, Mode::Enumeration);
    EXPECT_DOUBLE_EQ(r.equity[0], 0.0);
    EXPECT_DOUBLE_EQ(r.equity[1], 1.0);
}

// Preflop AA vs KK: the primary encoding/evaluator regression anchor.
// Golden: ~82.6366% / ~17.3634% (per design-docs/00-overview.md).
// A wrong card-encoding contract (e.g. wrong suit order) changes this silently.
TEST(CalculatorIntegration, AAvsKK_PreflopGolden) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"AhAs", "KhKs"}), Board{}, 0, cfg);
    Result r = calc.compute();

    EXPECT_EQ(r.mode, Mode::Enumeration);
    EXPECT_NEAR(r.equity[0], 0.826366, 5e-4);
    EXPECT_NEAR(r.equity[1], 0.173634, 5e-4);
}

// AhKs vs AdKc: suit-isomorphic hands (swap h↔d, s↔c maps each to the other).
// Equities must be bit-equal on the deterministic exact path.
TEST(CalculatorIntegration, MirroredRangesSplitEvenly) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"AhKs", "AdKc"}), Board{}, 0, cfg);
    Result r = calc.compute();

    EXPECT_EQ(r.mode, Mode::Enumeration);
    EXPECT_NEAR(r.equity[0], r.equity[1], 1e-9);
}

// ── Category 2: Accounting invariants ────────────────────────────────────────
// These hold for any valid result and catch normalization bugs even without a
// golden value.

// All per-player equities lie in [0, 1] and their sum equals exactly 1.0.
TEST(CalculatorIntegration, EquitiesSumToOne) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"AhAs", "KhKs", "QdQc"}), Board{}, 0, cfg);
    Result r = calc.compute();

    double sum = 0.0;
    for (unsigned i = 0; i < r.players; ++i) {
        EXPECT_GE(r.equity[i], 0.0);
        EXPECT_LE(r.equity[i], 1.0);
        sum += r.equity[i];
    }
    EXPECT_NEAR(sum, 1.0, 1e-9);
}

// totalWeightTallied and totalScenarios must be non-zero on any valid run.
TEST(CalculatorIntegration, DenominatorNonzero) {
    Calculator calc(ranges({"AhKs", "QdQc"}), Board{}, 0, {});
    Result r = calc.compute();
    EXPECT_GT(r.totalWeightTallied, 0.0);
    EXPECT_GT(r.totalScenarios, 0u);
}

// Down-weighting a dominant holding must reduce that range's equity vs. the
// equal-weight baseline. Fails if the denominator is a count or a precomputed
// product of range sizes rather than the per-leaf accumulated weight.
TEST(CalculatorIntegration, WeightAffectsEquity) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Result base = Calculator(ranges({"AhAs,KhKs", "QdQc"}),
                             Board{}, 0, cfg).compute();
    Result wtd  = Calculator(ranges({"AhAs:0.1,KhKs", "QdQc"}),
                             Board{}, 0, cfg).compute();
    // Halving AhAs drags P1's equity down relative to the equal-weight baseline.
    EXPECT_LT(wtd.equity[0], base.equity[0]);
}

// ── Category 3: Tie / split-pot correctness ───────────────────────────────────
// Exercises ties[], wins[], and the winsByPlayerMask joint-outcome tally.

// Royal flush AhKhQhJhTh on board → neither 2h3d nor 4c5s can beat it.
// Guaranteed chop: equity 0.5/0.5, weight entirely in the both-players bucket.
TEST(CalculatorIntegration, ForcedChopSplitsEvenly) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"2h3d", "4c5s"}),
                    Board::fromMask(maskOf("AhKhQhJhTh")),
                    0, cfg);
    Result r = calc.compute();

    EXPECT_NEAR(r.equity[0], 0.5, 1e-9);
    EXPECT_NEAR(r.equity[1], 0.5, 1e-9);
    EXPECT_NEAR(r.ties[0],   r.ties[1], 1e-9);
    // All weight in the both-players-tie mask; single-winner masks must be empty.
    EXPECT_GT  (r.winsByPlayerMask[0b11], 0.0);
    EXPECT_NEAR(r.winsByPlayerMask[0b01], 0.0, 1e-9);
    EXPECT_NEAR(r.winsByPlayerMask[0b10], 0.0, 1e-9);
}

// ── Category 4: Mode selection ────────────────────────────────────────────────

// River spot with single hands: 1 matchup × 0 runouts = 1 scenario.
TEST(CalculatorIntegration, SmallSpotSelectsEnumeration) {
    Calculator calc(ranges({"AhKs", "QdQc"}),
                    Board::fromMask(maskOf("2h7s9dTcJh")), 0, {});
    EXPECT_EQ(calc.compute().mode, Mode::Enumeration);
}

// Wide preflop ranges: ~210 * ~198 matchups × C(48,5) runouts ≈ 71B > 50M.
TEST(CalculatorIntegration, LargeSpotSelectsMonteCarlo) {
    CalculatorConfig cfg;
    cfg.mcSeed = 12345;  // seed: MC path must be deterministic in CI
    Calculator calc(ranges({"22+,A2s+,A7o+", "22+,A2s+,A9o+,KQo"}),
                    Board{}, 0, cfg);
    Result r = calc.compute();
    EXPECT_EQ(r.mode, Mode::MonteCarlo);
    EXPECT_GT(r.ciHalfWidth, 0.0);  // MC reports CI; enumeration reports 0
}

// QQ vs JJ: ~61.6M scenarios > 50M default → MC. Raise threshold → flips exact.
TEST(CalculatorIntegration, ThresholdForcesExact) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"QQ", "JJ"}), Board{}, 0, cfg);
    Result r = calc.compute();
    EXPECT_EQ(r.mode, Mode::Enumeration);
    EXPECT_DOUBLE_EQ(r.ciHalfWidth, 0.0);  // exact path emits no CI
}

// ── Category 5: Exact-vs-MC agreement ────────────────────────────────────────

// QQ vs JJ: exact is the oracle; seeded MC must agree within 3× its own CI.
TEST(CalculatorIntegration, MonteCarloAgreesWithExact) {
    CalculatorConfig exact;
    exact.enumThreshold = UINT64_MAX;
    Result truth = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, exact).compute();
    ASSERT_EQ(truth.mode, Mode::Enumeration);

    CalculatorConfig mc;
    mc.mcSeed    = 99;
    mc.mcCiTarget = 1e-4;  // tighter than default for a stable assertion
    Result est = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, mc).compute();
    ASSERT_EQ(est.mode, Mode::MonteCarlo);

    // 3× CI gives generous headroom without masking truly wrong estimates.
    // Floor of 2e-3 guards the case where the CI is reported very small.
    double bar = std::max(est.ciHalfWidth * 3.0, 2e-3);
    EXPECT_NEAR(est.equity[0], truth.equity[0], bar);
    EXPECT_NEAR(est.equity[1], truth.equity[1], bar);
}

// Same seed must produce bit-identical results — the precondition that makes
// every other seeded MC test trustworthy in CI.
TEST(CalculatorIntegration, SeededMonteCarloIsDeterministic) {
    CalculatorConfig cfg;
    cfg.mcSeed = 7;
    Result a = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, cfg).compute();
    Result b = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, cfg).compute();
    ASSERT_EQ(a.mode, Mode::MonteCarlo);
    EXPECT_DOUBLE_EQ(a.equity[0], b.equity[0]);
    EXPECT_DOUBLE_EQ(a.equity[1], b.equity[1]);
}

// ── Category 6: Conflict removal (board / dead cards) ────────────────────────

// Board Kd2s3s contains Kd. P1's KdKh combo shares Kd → removed.
// P1's live range becomes {QdQh} alone. Result must match providing {QdQh}
// directly — proving removeConflicts fired before enumeration.
TEST(CalculatorIntegration, BoardCardRemovesConflictingCombo) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Board board = Board::fromMask(maskOf("Kd2s3s"));

    Result with_conflict = Calculator(ranges({"KdKh,QdQh", "AcAs"}),
                                      board, 0, cfg).compute();
    Result live_only     = Calculator(ranges({"QdQh", "AcAs"}),
                                      board, 0, cfg).compute();
    EXPECT_NEAR(with_conflict.equity[0], live_only.equity[0], 1e-9);
    EXPECT_NEAR(with_conflict.equity[1], live_only.equity[1], 1e-9);
}

// Dead-card mask removes those cards from all runouts. Result must still be a
// valid normalized output (equities sum to 1, denominator non-zero).
TEST(CalculatorIntegration, DeadMaskExcludesCards) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    uint64_t dead = maskOf("2h2s");
    Calculator calc(ranges({"AhKs", "QdQc"}), Board{}, dead, cfg);
    Result r = calc.compute();
    EXPECT_NEAR(r.equity[0] + r.equity[1], 1.0, 1e-9);
    EXPECT_GT(r.totalWeightTallied, 0.0);
}

// ── Edge cases ────────────────────────────────────────────────────────────────

// 4 specific hands: equities must sum to 1.0 regardless of player count.
TEST(CalculatorIntegration, FourWayNormalizes) {
    CalculatorConfig cfg;
    cfg.mcSeed = 1;  // seed in case scenario product exceeds threshold
    Calculator calc(ranges({"AhAs", "KhKs", "QdQc", "JsJh"}), Board{}, 0, cfg);
    Result r = calc.compute();
    EXPECT_EQ(r.players, 4u);
    double sum = 0.0;
    for (unsigned i = 0; i < 4; ++i) sum += r.equity[i];
    EXPECT_NEAR(sum, 1.0, 5e-3);
}

// equity[i] == (wins[i] + ties[i]) / totalWeightTallied for every player.
// Pins the documented relationship between the per-player breakdown and the
// normalized equity — catches any mismatch in buildResult's accumulation.
TEST(CalculatorIntegration, EquityMatchesWinsPlusTies) {
    CalculatorConfig cfg;
    cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"AhAs", "KhKs"}), Board{}, 0, cfg);
    Result r = calc.compute();
    for (unsigned i = 0; i < r.players; ++i) {
        double expected = (r.wins[i] + r.ties[i]) / r.totalWeightTallied;
        EXPECT_NEAR(r.equity[i], expected, 1e-9);
    }
}

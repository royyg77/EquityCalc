# 09 — Integration Tests (Calculator end-to-end)

This doc specifies the **integration test suite** for EquityCalc v1.0. It is the
companion to the per-class unit tests already written (`test_card`, `test_combo`,
`test_range`, `test_board`, `test_parser`, `test_evaluator`).

Where those tests verify each class **in isolation**, this suite verifies the
classes **wired together** through the real public pipeline:

```
parseRange(str) ─► Range ─┐
parseRange(str) ─► Range ─┼─► Calculator(ranges, board, deadMask, cfg) ─► compute() ─► Result
Board::fromMask(mask) ────┘
```

That chain *is* the integration. Every test below constructs ranges through
`parseRange` (never by hand-building combos — we want the Parser in the path),
builds the `Calculator`, calls `compute()`, and asserts on the returned `Result`.

**Framework:** GoogleTest, same as the unit suites. Integration vs. unit is a
distinction about *scope*, not *tooling* — these are ordinary `TEST(...)` cases
run by the same `ctest`. No new dependency.

**File:** `engine/tests/test_calculator.cpp`
**Register** in `engine/tests/CMakeLists.txt` alongside the other test
executables (same `add_executable` + `gtest_discover_tests` pattern already used).

---

## Why this suite exists (the bug class it catches)

Unit tests cannot catch **correct-in-isolation, wrong-when-combined** bugs. The
canonical example is the card-encoding contract in `00-overview.md`: Combo can
build a valid `omp::Hand`, Evaluator can score a valid hand, and yet if suit
ordering disagrees at the OMP boundary the *combination* yields silently wrong
equities while every unit test stays green. The overview names this "the single
most dangerous class of bug in the project."

The **known-equity anchors** (Category 1) are the canary for exactly that. If
AA-vs-KK stops reading 82.6366%, the encoding or the evaluator wiring broke —
and only an end-to-end test through `Calculator::compute()` sees it.

This suite is also the **correctness gate for v1.1 optimization.** Threading,
suit isomorphism, libdivide — all must preserve results exactly. These tests are
what go red the moment an optimization is fast but wrong.

---

## Real interfaces (use these exact signatures — do not invent)

From the actual headers:

```cpp
// Parser.h
Range parseRange(std::string_view input);          // throws ParseError on bad input
Card  parseCard(std::string_view s);

// Range.h
class Range { /* built via parseRange; pass by value into Calculator */ };

// Board.h
class Board {
    Board();                                        // empty board
    static Board fromMask(uint64_t mask);
    // ...
};

// Result.h
static constexpr unsigned MAX_PLAYERS = 6;
enum class Mode { Enumeration, MonteCarlo };
struct Result {
    unsigned players;
    double   equity[MAX_PLAYERS];                   // per-player, [0,1], sums ~1.0
    double   wins[MAX_PLAYERS];
    double   ties[MAX_PLAYERS];
    double   winsByPlayerMask[1 << MAX_PLAYERS];    // joint outcome by winner bitmask
    uint64_t totalScenarios;
    double   totalWeightTallied;                    // equity denominator
    double   timeSeconds;
    Mode     mode;
    double   ciHalfWidth;                           // 0 for enumeration
};

// Calculator.h
struct CalculatorConfig {
    unsigned threadCount{1};
    double   mcCiTarget{2e-4};
    uint64_t enumThreshold{50'000'000};
    uint64_t mcSeed{0};
};
class Calculator {
public:
    explicit Calculator(std::vector<Range> ranges,
                        Board            board    = Board{},
                        uint64_t         deadMask = 0,
                        CalculatorConfig cfg      = {});
    Result compute();
};
```

### A small mask helper (define once at top of the test file)

Board construction needs a `uint64_t` mask. Build it from card strings via the
Parser so tests read naturally and stay on the encoding contract:

```cpp
namespace {
using namespace equitycalc;

// "2h7s9dTcJh" -> bitmask. Uses parseCard so the encoding path is exercised.
uint64_t maskOf(std::string_view cards) {
    uint64_t m = 0;
    for (size_t i = 0; i + 1 < cards.size(); i += 2)
        m |= uint64_t{1} << parseCard(cards.substr(i, 2)).value;
    return m;
}

// Convenience: build the ranges vector from string literals.
std::vector<Range> ranges(std::initializer_list<std::string_view> rs) {
    std::vector<Range> v;
    for (auto r : rs) v.push_back(parseRange(r));
    return v;
}
} // namespace
```

---

## Test design principles (apply to every case below)

1. **Anchors stay exact.** Category 1 numeric anchors must run on the
   enumeration path so there is zero sampling variance and the assertion can be
   exact (or near-exact). Force this with `cfg.enumThreshold = UINT64_MAX`.
2. **MC tests must seed.** Any test that intentionally exercises Monte Carlo
   sets `cfg.mcSeed` to a fixed nonzero value so the run is deterministic and
   the assertion is stable in CI. Never assert on an unseeded MC run.
3. **Assert properties, not just numbers.** Category 2 invariants hold for *any*
   valid result and catch normalization bugs even where no golden exists.
4. **Never assert on timing.** `timeSeconds` is environment-dependent and flaky
   in CI. This suite asserts correctness only; timing lives in the benchmark.
5. **Use the Parser path.** Build ranges with `parseRange`, never by hand-
   constructing `Combo`s — bypassing the Parser would skip part of the
   integration we are trying to test.

---

## Category 1 — Known-equity anchors

The canaries. Externally-verifiable answers, forced onto the exact path.

### 1a. Complete-board, deterministic winner (zero variance)
A full 5-card board leaves exactly one runout, so the winner is deterministic
and equity is exactly 1.0 / 0.0. The cleanest possible end-to-end assertion.

```cpp
TEST(CalculatorIntegration, CompleteBoardDeterministicWinner) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;   // force exact
    Calculator calc(ranges({"AhKs", "QdQc"}),
                    Board::fromMask(maskOf("2h7s9dTcJh")),   // QQ makes a pair; AK misses
                    0, cfg);
    Result r = calc.compute();

    EXPECT_EQ(r.players, 2u);
    EXPECT_EQ(r.mode, Mode::Enumeration);
    // Determine the actual winner from the board and pin BOTH equities exactly.
    // (Set the expected winner per the real evaluation; on 2h7s9dTcJh, QdQc has
    //  a pair of queens, AhKs has ace-high → P2 wins outright.)
    EXPECT_DOUBLE_EQ(r.equity[0], 0.0);
    EXPECT_DOUBLE_EQ(r.equity[1], 1.0);
}
```

> **Implementation note for Claude Code:** verify the winner on this exact board
> by reasoning about the cards before pinning the values; do not assume P2.
> If the chosen board makes the winner ambiguous, pick a board where it is not.

### 1b. AA vs KK preflop — THE encoding/evaluator anchor
The verified golden is **0.826366 / 0.173634** (per `00-overview.md` and the
benchmark's S2 spot). Forced exact; tight tolerance.

```cpp
TEST(CalculatorIntegration, AAvsKK_PreflopGolden) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;   // exact enumeration
    Calculator calc(ranges({"AhAs", "KhKs"}), Board{}, 0, cfg);
    Result r = calc.compute();

    EXPECT_EQ(r.mode, Mode::Enumeration);
    EXPECT_NEAR(r.equity[0], 0.826366, 5e-4);
    EXPECT_NEAR(r.equity[1], 0.173634, 5e-4);
}
```

### 1c. Symmetric matchup — mirror equity
Identical-strength ranges must split ~50/50. Catches any left/right asymmetry
in the matchup decode or tally.

```cpp
TEST(CalculatorIntegration, MirroredRangesSplitEvenly) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    // Two single hands of equal preflop strength by suit symmetry.
    Calculator calc(ranges({"AhKs", "AdKc"}), Board{}, 0, cfg);
    Result r = calc.compute();
    EXPECT_NEAR(r.equity[0], r.equity[1], 1e-9);  // exact mirror on exact path
}
```

---

## Category 2 — Accounting invariants (hold for any valid result)

No golden needed; these assert structural properties.

### 2a. Equities sum to ~1.0 and lie in [0,1]
```cpp
TEST(CalculatorIntegration, EquitiesSumToOne) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
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
```

### 2b. Denominator is populated
```cpp
TEST(CalculatorIntegration, DenominatorNonzero) {
    Calculator calc(ranges({"AhKs", "QdQc"}), Board{}, 0, {});
    Result r = calc.compute();
    EXPECT_GT(r.totalWeightTallied, 0.0);
    EXPECT_GT(r.totalScenarios, 0u);
}
```

### 2c. Weighting changes equity in the expected direction
Down-weighting a strong holding in a range should lower that range's equity vs.
the unweighted baseline. Asserts the weight actually flows into the denominator.

```cpp
TEST(CalculatorIntegration, WeightAffectsEquity) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    // Baseline: {AA, KK} vs QQ, both holdings full weight.
    Result base = Calculator(ranges({"AhAs,KhKs", "QdQc"}), Board{}, 0, cfg).compute();
    // Down-weight the strong AA to 0.1; range should fare worse.
    Result wtd  = Calculator(ranges({"AhAs:0.1,KhKs", "QdQc"}), Board{}, 0, cfg).compute();
    EXPECT_LT(wtd.equity[0], base.equity[0]);
}
```

---

## Category 3 — Tie / split-pot correctness

Exercises `ties[]` and the multiway `winsByPlayerMask` bitmask tally — the
subtlest accounting in the Calculator.

### 3a. Forced chop splits equity evenly
Pick a complete board where both players play the same five-card hand (the board
plays), forcing a two-way chop. Equity 0.5/0.5, with the contribution landing in
the **both-players** bucket, not two separate single-player wins.

```cpp
TEST(CalculatorIntegration, ForcedChopSplitsEvenly) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    // Board plays a straight/flush both players share. Choose cards so neither
    // hole pair improves on the board's five-card hand.
    // (Claude Code: select a concrete board where the board is the nut hand for
    //  both holdings, e.g. a board straight neither hole card beats.)
    Calculator calc(ranges({"2h3d", "4c5s"}),
                    Board::fromMask(maskOf("AhKhQhJhTh")), // royal on board → chop
                    0, cfg);
    Result r = calc.compute();

    EXPECT_NEAR(r.equity[0], 0.5, 1e-9);
    EXPECT_NEAR(r.equity[1], 0.5, 1e-9);
    EXPECT_NEAR(r.ties[0], r.ties[1], 1e-9);
    // Joint bucket: both-players-tie mask (0b11) carries the weight; the
    // single-winner buckets (0b01, 0b10) should be ~0 for a guaranteed chop.
    EXPECT_GT(r.winsByPlayerMask[0b11], 0.0);
    EXPECT_NEAR(r.winsByPlayerMask[0b01], 0.0, 1e-9);
    EXPECT_NEAR(r.winsByPlayerMask[0b10], 0.0, 1e-9);
}
```

> **Note:** with a royal flush on the board, every player ties regardless of hole
> cards — a clean, unambiguous chop. Confirm the chosen board genuinely forces
> the tie before pinning the bucket assertions.

---

## Category 4 — Mode selection

Reads `result.mode`; sizes input to land on each path. `enumThreshold` is the
knob.

### 4a. Tiny spot enumerates
```cpp
TEST(CalculatorIntegration, SmallSpotSelectsEnumeration) {
    // River, single hands: 1 scenario. Far under any threshold.
    Calculator calc(ranges({"AhKs", "QdQc"}),
                    Board::fromMask(maskOf("2h7s9dTcJh")), 0, {});
    EXPECT_EQ(calc.compute().mode, Mode::Enumeration);
}
```

### 4b. Large spot selects Monte Carlo
```cpp
TEST(CalculatorIntegration, LargeSpotSelectsMonteCarlo) {
    CalculatorConfig cfg; cfg.mcSeed = 12345;  // seed: MC path must be deterministic
    // Wide preflop ranges push the scenario product past the 50M default.
    Calculator calc(ranges({"22+,A2s+,A7o+", "22+,A2s+,A9o+,KQo"}), Board{}, 0, cfg);
    Result r = calc.compute();
    EXPECT_EQ(r.mode, Mode::MonteCarlo);
    EXPECT_GT(r.ciHalfWidth, 0.0);   // MC reports a CI; enumeration reports 0
}
```

### 4c. Threshold boundary is honored
Force a normally-MC spot exact by raising the threshold; assert it flips.

```cpp
TEST(CalculatorIntegration, ThresholdForcesExact) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"QQ", "JJ"}), Board{}, 0, cfg);  // ~61.6M scenarios
    Result r = calc.compute();
    EXPECT_EQ(r.mode, Mode::Enumeration);
    EXPECT_DOUBLE_EQ(r.ciHalfWidth, 0.0);  // exact path reports no CI
}
```

---

## Category 5 — Exact-vs-MC agreement (self-checking, no external golden)

The strongest correctness check for the MC path: the exact run is the oracle.
QQ-vs-JJ, once forced exact, once seeded MC; assert agreement within the MC
tolerance.

```cpp
TEST(CalculatorIntegration, MonteCarloAgreesWithExact) {
    // Oracle: exact.
    CalculatorConfig exact; exact.enumThreshold = UINT64_MAX;
    Result truth = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, exact).compute();
    ASSERT_EQ(truth.mode, Mode::Enumeration);

    // Estimate: seeded MC at the default threshold (QQ/JJ auto-selects MC).
    CalculatorConfig mc; mc.mcSeed = 99; mc.mcCiTarget = 1e-4;  // tighten for a stable check
    Result est = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, mc).compute();
    ASSERT_EQ(est.mode, Mode::MonteCarlo);

    // Agreement within a few CI half-widths is the right bar (statistical, not exact).
    double bar = std::max(est.ciHalfWidth * 3.0, 2e-3);
    EXPECT_NEAR(est.equity[0], truth.equity[0], bar);
    EXPECT_NEAR(est.equity[1], truth.equity[1], bar);
}
```

> **Why `ciHalfWidth * 3`:** the CI is ~95% (≈2σ); using 3× gives generous
> headroom so the test is not flaky at the boundary while still catching a truly
> wrong estimate. The `2e-3` floor guards the case where the CI is reported very
> small.

### 5b. Same seed → identical MC result (determinism)
Proves seeding actually controls the RNG — the precondition that makes every
other MC test trustworthy.

```cpp
TEST(CalculatorIntegration, SeededMonteCarloIsDeterministic) {
    CalculatorConfig cfg; cfg.mcSeed = 7;
    Result a = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, cfg).compute();
    Result b = Calculator(ranges({"QQ", "JJ"}), Board{}, 0, cfg).compute();
    ASSERT_EQ(a.mode, Mode::MonteCarlo);
    EXPECT_DOUBLE_EQ(a.equity[0], b.equity[0]);
    EXPECT_DOUBLE_EQ(a.equity[1], b.equity[1]);
}
```

---

## Category 6 — Conflict removal (board / dead cards)

A combo overlapping the board or a dead card must be dropped before tallying.

### 6a. Dead card removes a combo from a range
Compare a range's combo count effect by choosing a board that collides with part
of one player's range, and assert the equity shifts vs. the no-collision case.
The cleanest observable: a specific hand that *uses* a board card cannot be held.

```cpp
TEST(CalculatorIntegration, BoardCardRemovesConflictingCombo) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    // P1 range "AhAs,AhKs" — the AhKs combo conflicts with any board containing
    // Ah... but Ah is in BOTH combos, so use a cleaner collision:
    // P1 "KdKh,QdQh" vs P2 "AcAs"; board "Kd2s3s" kills KdKh (shares Kd),
    // leaving only QdQh live for P1.
    Result with    = Calculator(ranges({"KdKh,QdQh", "AcAs"}),
                                Board::fromMask(maskOf("Kd2s3s")), 0, cfg).compute();
    Result without = Calculator(ranges({"QdQh", "AcAs"}),
                                Board::fromMask(maskOf("Kd2s3s")), 0, cfg).compute();
    // With KdKh removed by conflict, P1's effective range == {QdQh}, so the two
    // results must match.
    EXPECT_NEAR(with.equity[0], without.equity[0], 1e-9);
}
```

### 6b. Explicit dead-card mask excludes those cards
```cpp
TEST(CalculatorIntegration, DeadMaskExcludesCards) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    uint64_t dead = maskOf("2h2s");  // remove deuces from the deck
    Calculator calc(ranges({"AhKs", "QdQc"}), Board{}, dead, cfg);
    Result r = calc.compute();
    // Sanity: still a valid, normalized result with the smaller deck.
    double sum = r.equity[0] + r.equity[1];
    EXPECT_NEAR(sum, 1.0, 1e-9);
    EXPECT_GT(r.totalWeightTallied, 0.0);
}
```

---

## Edge / robustness cases

### E1. Multiway (3–6 players) still normalizes
```cpp
TEST(CalculatorIntegration, FourWayNormalizes) {
    CalculatorConfig cfg; cfg.mcSeed = 1;  // likely MC; seed for stability
    Calculator calc(ranges({"AhAs", "KhKs", "QdQc", "JsJh"}), Board{}, 0, cfg);
    Result r = calc.compute();
    EXPECT_EQ(r.players, 4u);
    double sum = 0; for (unsigned i = 0; i < 4; ++i) sum += r.equity[i];
    EXPECT_NEAR(sum, 1.0, 5e-3);  // MC tolerance
}
```

### E2. `wins[i] + ties[i]` is consistent with `equity[i]`
Per-player equity should equal (outright wins + tie-share) over the denominator.
Assert the documented relationship rather than recomputing it blindly — confirm
against the real `buildResult` definition before pinning the exact formula.

```cpp
TEST(CalculatorIntegration, EquityMatchesWinsPlusTies) {
    CalculatorConfig cfg; cfg.enumThreshold = UINT64_MAX;
    Calculator calc(ranges({"AhAs", "KhKs"}), Board{}, 0, cfg);
    Result r = calc.compute();
    for (unsigned i = 0; i < r.players; ++i) {
        double expected = (r.wins[i] + r.ties[i]) / r.totalWeightTallied;
        EXPECT_NEAR(r.equity[i], expected, 1e-9);
    }
}
```

> **Claude Code:** if `wins`/`ties` are stored as raw weighted counts and
> `equity` is already normalized, this relationship holds as written. If the
> field semantics differ, adjust the formula to match `Result`'s actual
> definitions — do **not** change `Result` to fit the test.

---

## CMake registration

Add to `engine/tests/CMakeLists.txt`, mirroring the existing test pattern:

```cmake
add_executable(test_calculator test_calculator.cpp)
target_link_libraries(test_calculator PRIVATE equitycalc_engine GTest::gtest_main)
gtest_discover_tests(test_calculator)
```

(Match the real engine library target name and GTest target names already used
by the other `test_*` executables — copy from `test_evaluator`'s entry.)

---

## Implementation checklist for Claude Code

1. Create `engine/tests/test_calculator.cpp` with the `maskOf` / `ranges`
   helpers, then the cases above grouped by category with comment headers.
2. **Verify every concrete board/winner before pinning a value.** Several cases
   (1a, 3a, 6a) say "confirm the winner/chop on this exact board." Do that
   reasoning; do not assume. A wrong golden here is worse than no test.
3. Force exact with `cfg.enumThreshold = UINT64_MAX` on all Category 1/2/3/6
   numeric anchors. Seed (`cfg.mcSeed`) on every MC case.
4. Never assert on `timeSeconds`.
5. Register in `engine/tests/CMakeLists.txt`; confirm `ctest` discovers and runs
   `test_calculator`.
6. Run `cmake --build build && ctest --test-dir build --output-on-failure` and
   ensure all green before considering this done.

## Invariants this suite pins (the "why" per case)

- **Encoding contract** → 1a, 1b (wrong suits ⇒ wrong equity ⇒ red).
- **Normalization / denominator** → 2a, 2b, 2c, E2.
- **Split-pot tally** (`winsByPlayerMask`, `ties`) → 3a.
- **Mode selection by scenario count** → 4a, 4b, 4c.
- **MC correctness** (vs. exact oracle) and **reproducibility** → 5a, 5b.
- **Conflict removal** (board + dead) → 6a, 6b.
- **N-player generality** → E1.

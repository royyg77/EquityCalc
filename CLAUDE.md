# CLAUDE.md

Persistent context for Claude Code on the EquityCalc project. Keep this file short and current; per-class detail lives in `design-docs/`.

## What this is

EquityCalc is a poker equity calculator. Given hand ranges (and optionally a board + dead cards), it answers "what % of the time does each player win?" by simulating completed boards. A C++ engine does the work.

## Scope: v1.0 is CLI only

The deliverable for v1.0 is a single command-line binary. **No pybind11, no FastAPI, no React.** Those layers exist in the repo skeleton but are deferred to v2.0. Do not generate code for `bindings/`, `backend/`, or `frontend/` during v1.0 work.

Expected v1.0 usage:

```
$ equitycalc "AhKs" "QdQc"
$ equitycalc "AKs" "QQ+" --board "2c7d9s"
$ equitycalc "AA" "KK" "QQ" --board "2c7d" --dead "AcAd"
$ equitycalc --range-file p1.txt --range-file p2.txt --mc --target-stdev 0.001
```

## Architecture

```
CLI ──► Parser ──► Range ──► Combo ──► Card
                                 └─► omp::Hand
       └─► Calculator ──► Range
              ├─► Board ──► omp::Hand
              ├─► Evaluator ──► omp::HandEvaluator
              └─► Result
```

Seven classes plus a Parser free-function module. Dependencies point one direction (no cycles). Build and test bottom-up.

## Source of truth for class design

**Read `design-docs/` before generating or modifying any class.** Per-class files exist for Card, Combo, Range, Board, Parser, Evaluator, Calculator, Result. Each doc contains:

- **Invariants** — the contract the implementation must uphold
- **Hard parts** — design decisions that matter, including OMP boundary specifics
- **Pseudocode** — the intended shape of non-trivial methods
- **Test cases to expect** — the concrete tests that should accompany the class

If a generated class violates an invariant in its doc, that's drift. Correct it before proceeding.

`design-docs/00-overview.md` carries the shared conventions; read it first.

## The OMPEval boundary

OMPEval is vendored at `engine/third_party/OMPEval/` (ISC license). Used at runtime: `Hand.h`, `HandEvaluator.{h,cpp}`, `Constants.h`, `Util.h`, `OffsetTable.hxx`. The other OMP files (`CardRange.*`, `CombinedRange.*`, `EquityCalculator.*`, `Random.h`) are vendored for *reference only* and get deleted as our equivalents ship.

**Only three of our files include an OMP header:**

- `combo.h` — carries an `omp::Hand` (pre-built and cached)
- `board.h` — carries an `omp::Hand` accumulator
- `evaluator.h` — holds an `omp::HandEvaluator`

Every other class is OMP-free at the include level. **Never edit anything under `third_party/`.** If OMP needs different behavior, wrap it on our side.

## Critical locked decisions

These are non-negotiable; do not "improve" them in generated code.

### Card encoding contract

`Card.value = rank * 4 + suit`. Ranks 0–12 (2 through A). **Suits in OMP order: s=0, h=1, c=2, d=3. NOT alphabetical c/d/h/s.** Alphabetical suit display is a presentation-layer concern only — the internal `value` must match OMP's card index exactly, or `omp::Hand(card.value)` silently scores the wrong cards.

This must be pinned by a `static_assert` or unit test: `Card(12, 1).value == 49` and `Card(0, 0).value == 0`. Without that guard, future "cleanup" can break the contract invisibly.

### No sentinel, no default Card

`Card` is not default-constructible and has no "no card" value. `Board` stores an `omp::Hand` accumulator + a `cardMask` + a count — no `std::array<Card,5>` — so there are no undealt slots that would need a sentinel. Every constructed `Card` is a real card.

### Board uses `omp::Hand::empty()`

`Board`'s internal `omp::Hand` must be initialized from `omp::Hand::empty()`, **never default-constructed**. The empty constructor pre-seeds suit counters to 3 so flush detection works; a default `omp::Hand` is uninitialized garbage and produces silently wrong evaluations.

### Combo caches the omp::Hand

`Combo` builds its `omp::Hand` once in the constructor and reuses it forever. The inner loop combines `combo.evalHand + board.evalHand` with a single SSE add. **Do not rebuild `omp::Hand` from card indices inside the loop.** This is the single biggest performance drift risk; check it.

### Mode selection is by scenario count, not player count

```
totalScenarios = preflopCombos * postflopBoards
if totalScenarios < THRESHOLD: enumerate
else:                          monte carlo
```

Threshold lands ~1e7–1e8 (tunable). Player count falls out naturally — 6-way preflop hits MC; a 6-way river spot enumerates. Do not hardcode "N players → MC" rules.

### Max 6 players

`MAX_PLAYERS = 6`. The `winsByPlayerMask` tally array is `2^MAX_PLAYERS = 64` entries. v1.0 supports 2–6.

### Single shared Evaluator across threads

`omp::HandEvaluator::evaluate` is `const` and read-only after the one-time static table init. Construct one `Evaluator` in `Calculator`, share it across all worker threads. **Do not construct one per thread.**

### Inline conflict skip for v1.0

The Calculator's outer loop does an inline `c1.cardMask & c2.cardMask` check to skip conflicting combo pairings. **Do not build `CombinedRange`-style precomputation in v1.0** — that's a v1.5 optimization for heavily-overlapping multi-player MC, and it's ~200 lines that buys nothing in the 2–3 player case.

### Weights: float per combo, last-write-wins dedup

`Combo` has a `float weight`. `Range::normalize()` dedups by cards; when two specifications conflict on weight, the later one wins. Equity normalizes by **total weight tallied**, not scenario count.

### Strict parser errors

The Parser produces clear error messages with positions, not OMP's silent-ignore behavior. A bad token aborts parsing with a useful message.

## Build & test commands

```bash
cd engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cli/equitycalc                       # smoke test
ctest --test-dir build --output-on-failure   # all tests
```

Tests are required for each class before moving to the next. The test files exist in `engine/tests/` (one per class).

## Performance targets

- 2-player hand vs hand, fixed board: < 50ms
- 2-player hand vs range, fixed board: < 100ms
- 2–3 player range vs range: enumeration when feasible, MC otherwise; finishes in a few seconds
- 4–6 player anything: MC; converges to 95% CI half-width < 0.5% in a few seconds

Flag any change that risks blowing these. Hot-path changes need a before/after benchmark.

## Code style

- **C++**: C++20. Headers in `include/equitycalc/`, sources in `src/`. Prefer `std::` containers; avoid raw `new`/`delete`. `constexpr` and `noexcept` where they fit. clang-format LLVM style, 4-space indent.
- **Naming**: `snake_case` files, `PascalCase` types, `camelCase` methods, trailing-underscore private members (`combos_`).
- **Commits**: one logical change per commit; imperative subject ("Add range parser").

## Build order (v1.0)

In strict order, with full tests for each before the next:

1. `Card` (per `design-docs/01-card.md`)
2. `Combo` (per `02-combo.md`) — first OMP boundary
3. `Range` (per `03-range.md`)
4. `Board` (per `04-board.md`) — second OMP boundary
5. `Parser` (per `05-parser.md`)
6. `Evaluator` (per `06-evaluator.md`) — third OMP boundary
7. `Calculator`: 2-player hand vs hand, fixed board first; expand from there (per `07-calculator.md`)
8. `Calculator`: hand vs range
9. `Calculator`: range vs range
10. `Calculator`: partial board enumeration
11. `Calculator`: 3+ players (N-way tally)
12. Threading
13. Monte Carlo mode
14. CLI wiring (arg parsing, output formatting, `--json`)
15. `Result` polish + final integration tests

Each step is 1–3 sessions. After step 11 you have a complete single-threaded engine; threading and MC are additive.

## What's done / what's next

**Done:**
- Repo skeleton, build files
- OMPEval vendored at `engine/third_party/OMPEval/`
- Per-class design docs in `design-docs/`
- Architectural decisions locked (this file)

**Next:** Build step 1 (`Card`) per `design-docs/01-card.md`.

## Working agreements

- **Read the design doc for a class before generating its code.** The Invariants and Hard parts sections are the contract.
- **Stay in scope.** "Add range parser" means add range parser. Drive-by improvements get flagged as suggestions, not silently made.
- **Tests first when feasible.** For pure functions (parsers, evaluators, classifiers), write the test cases before the implementation.
- **Small commits.** One logical change per commit.
- **Never edit anything under `third_party/`.** Wrap, don't modify.
- **Don't add new dependencies without asking.** OMPEval is the only one currently planned.
- **Hot-path changes need benchmarks.** Anything in the inner loop (evaluation, enumeration, MC sampling) needs before/after numbers in the PR.
- **Update this file** when "What's done / next" drifts from reality, or when a locked decision changes (rare).

## Forward roadmap (after v1.0)

For context; do not implement these in v1.0 work.

- **v1.1**: `HandClassifier` and stats engine — Flopzilla-style range frequencies (made hands, draws, board textures). Additive to v1.0, no changes to existing classes.
- **v1.2**: Suit isomorphism in board enumeration + matchup deduplication via suit relabeling. The big enumeration speedup.
- **v1.3**: Precalculated 2-player preflop lookup table.
- **v1.5**: `CombinedRange`-style multi-player MC optimization.
- **v2.0**: pybind11 bindings + FastAPI backend + React frontend.

# Calculator

## Purpose

`Calculator` is the orchestrator. It takes the parsed inputs — one `Range` per
player, an optional fixed `Board`, an optional dead-card mask — selects a
computation mode, runs it, and returns a single `Result`. It owns the two
inner-loop drivers (exact enumeration and Monte Carlo), the shared scoring
function every scenario funnels through, and the accumulator that all results
land in. This is the largest and hardest class in the engine (~600 lines across
`.h` + `.cpp`).

It is also where the project's performance and scalability characteristics are
decided. The design below fixes four structural decisions up front so that later
optimizations (multithreading, `libdivide`, suit isomorphism, the 2-player
lookup table, the alias-table sampler) drop into clean seams instead of forcing
rewrites. **None of those optimizations are implemented in v1.0.** What v1.0
delivers is a correct, single-threaded engine whose *shape* is already
parallel-ready.

The single correctness property that matters most: **equities are normalized by
the summed weight of scenarios actually scored, never by a scenario count or a
precomputed product.** Everything in the accumulator design exists to make that
property impossible to violate.

---

## Vocabulary (used precisely throughout this doc)

- **Combo** — two specific hole cards + a weight + a cached `omp::Hand`
  (`evalHand`). One element of a player's `Range`. See `02-combo.md`.
- **Matchup** — one concrete assignment of a combo to *every* player. With `n`
  players this is an `n`-tuple of combos. The preflop space is the set of all
  matchups.
- **Runout** — one concrete completion of the board to 5 cards.
- **Scenario** — one (matchup, runout) pair. This is the atomic unit that gets
  *scored*. "Scenario" always means a fully-dealt, 5-card-board situation.
- **Scenario weight** — the weight a scenario contributes to the tally. In
  exact mode it is the product of the matchup's combo weights. In Monte Carlo
  mode it is always `1.0` (weighting is applied at sampling time instead — see
  the Monte Carlo driver).
- **Winner mask** — a bitmask over players: bit `i` set means player `i` is tied
  for (or holds) the best hand in a scored scenario. `0b011` = players 0 and 1
  chopped.

---

## The computation, stated once

Total work = every matchup × every runout for that matchup. The two sources of
variation (which cards players hold, which community cards come) are
independent, so the computation nests: matchups on the outside, runouts on the
inside, scoring at each leaf.

```
for each matchup:                 # outer: which hole cards
    if players share a card: skip # inter-player conflict
    for each runout:              # inner: which board cards
        score the scenario        # evaluate, find winners, tally
```

The two **modes** differ *only* in how they walk that structure:

- **Exact enumeration** walks every matchup (by index) and, for each, recurses
  through *every* runout. Used when the total scenario count is small enough.
- **Monte Carlo** *samples* a random matchup and a *single* random runout, over
  and over, until a stopping criterion is met. Used when exact would be too
  large.

Both modes converge on one shared scoring function and one shared accumulator.
The divergence is entirely in matchup-selection and runout-selection; the
convergence is everything from "a scenario is fully dealt" onward.

```
        EXACT DRIVER                      MONTE CARLO DRIVER
        enumerate()                       simulate()
   walk matchup index p=0..N-1       sample matchup (weighted draw)
   decode p -> combo per player      retry on inter-player conflict
   skip on inter-player conflict             |
          |                          sample ONE runout
   recurse ALL runouts                       |
          |                                  |
          +-------------- scoreScenario() ---+   <-- CONVERGENT
                               |
                          accumulator             <-- CONVERGENT
                               |
                          buildResult()           <-- runs once at end
```

---

## Four structural decisions (the scalability seams)

These are the load-bearing design choices. Each is implemented in its simplest
correct form for v1.0, but structured so the optimization drops in later.

### Decision 1 — Flat matchup index space (the threading seam)

The preflop matchup space is treated as a single 1-D index range `[0, N)`:

```
N = ranges_[0].size() * ranges_[1].size() * ... * ranges_[n-1].size()
```

(`size()` here is the post-conflict-removal combo count of each range — see
"Setup pipeline".) Any index `p` decodes to one combo per player by mixed-radix
("odometer") division — each player is a digit whose base is that player's range
size:

```
decodeMatchup(p, comboIdx[]):
    remaining = p
    for i in 0 .. n-1:
        comboIdx[i] = remaining % size_i
        remaining   = remaining / size_i
```

**Why this shape:** single-threaded exact enumeration is "process `p` from 0 to
N". Multithreaded enumeration later is "hand thread A the sub-range `[0, N/T)`,
thread B `[N/T, 2N/T)`, ...". *Same loop body, different bounds.* No
restructuring. Nested `for` loops could not be partitioned this way; the flat
index is what makes the threading seam free.

**v1.0 implementation:** plain `%` and `/`. The per-player divisors `size_i` are
read from a small precomputed array (`rangeSizes_[i]`), built once before the
loop — **never** recomputed inside it.

**Marked seam (do NOT implement now):** the `%`/`/` in `decodeMatchup` is the
one place `libdivide` plugs in later. When the inner loop has been optimized
enough that the preflop decode becomes measurable, replace the
`uint64_t rangeSizes_[]` array with a `libdivide::libdivide_u64_t dividers_[]`
array and swap the two operations for `libdivide_u64_do`. This is a ~5-line
change isolated to `decodeMatchup` + the setup that fills the array. `libdivide`
is a third-party header and is **not** a v1.0 dependency. Leave a comment at the
`rangeSizes_` declaration pointing here.

> Claude Code: do not add `libdivide` to the build or includes in v1.0. Plain
> integer division only. Just keep the divisors in an array and the decode in
> its own function so the later swap is mechanical.

### Decision 2 — Board enumeration recurses on `omp::Hand`, deck built once

The runout recursion does **not** carry a `Board` object and does **not** call
`Board::withCard()` per node. Instead:

1. Once per matchup, build the deck of available cards (every card index 0-51
   not used by a hole card or the fixed board) into a stack array `deck[]`.
2. Recurse on a bare `omp::Hand` board accumulator, threading
   `(deck, ndeck, start)` down. Each node does the one-SSE-add combine
   `board + omp::Hand(deck[i])` and advances `start` to `i+1` so each runout is
   visited once and never with a repeated card.

**Why this shape:** the expensive mistake is rebuilding the available-card set at
every recursion level. Building the deck once at the top and passing it down
makes the per-node cost a single SSE add + a table load. `Board::withCard()`
returns a `Board` by value (copies the `omp::Hand` *and* the `cardMask` and is
fine for constructing fixed boards) but is wrong for the hot path — the
recursion only needs the `omp::Hand`, not the `cardMask`, so it works on
`omp::Hand` directly.

**`Board` itself does not change.** The recursion seeds from `board_.hand()`
(the fixed board's `omp::Hand`) and `board_.count()` (how many cards remain to
deal). Both already exist (`Board.h` lines 35, 39). `withCard()` stays in the
class for non-hot-path use (tests, fixed-board construction); it is simply not
called inside the recursion.

**The combine and the no-overlap invariant:** `omp::Hand::operator+=` asserts the
two hands' card masks don't overlap (`Hand.h` line 73). Because `deck[]` contains
only unused cards and `start=i+1` prevents revisiting, this invariant holds
automatically. Do not add extra guards; rely on the deck construction.

**Marked seam (do NOT implement now):** OMP threads a `suitCounts[]` array
through its board recursion to detect isomorphic subtrees and irrelevant suits
(the ~3x "suit isomorphism" speedup). v1.0 does **not** carry `suitCounts`. When
suit isomorphism is added (v1.2), it threads through this same recursion.
Leave the recursion signature clean; note the seam in a comment.

### Decision 3 — A `BatchAccumulator` struct, not bare arrays

All scoring writes into one small struct that the running thread owns
exclusively. It has exactly three accumulating fields plus a `merge`:

```cpp
struct BatchAccumulator {
    // The ONE core field: joint-outcome tally indexed by winner mask.
    // winsByPlayerMask[0b011] = summed scenario weight where exactly P0 & P1 chopped.
    double   winsByPlayerMask[1u << MAX_PLAYERS] = {};

    // The denominator. Accumulated in LOCKSTEP with the tally (see Decision 4).
    double   totalWeightTallied = 0.0;

    // Raw count of scenarios scored. Drives MC stdev + Result.totalScenarios.
    uint64_t scenariosScored = 0;

    void merge(const BatchAccumulator& o) {
        for (unsigned m = 0; m < (1u << MAX_PLAYERS); ++m)
            winsByPlayerMask[m] += o.winsByPlayerMask[m];
        totalWeightTallied += o.totalWeightTallied;
        scenariosScored    += o.scenariosScored;
    }
};
```

**Why this shape:**

- *Parallel-ready.* The inner loop touches only thread-local memory — no shared
  writes, no locks in the hot path. Later, each thread owns one accumulator;
  results merge at batch/thread boundaries via `merge`. Because `merge` is plain
  elementwise addition (associative + commutative), the final result is
  identical regardless of how work was split across threads — **equities are
  deterministic under any thread count.**
- *One write per leaf.* Indexing the tally by *winner set* (not per player)
  means each scored scenario does a single `winsByPlayerMask[mask] += weight`,
  with no per-player loop and no branching at score time. All per-player
  breakdowns are derived later from this 64-entry array.
- *Lossless joint outcomes.* The full multiway breakdown (who-beat-whom sets) is
  preserved for `Result.winsByPlayerMask` and future v1.1 analysis.

`MAX_PLAYERS` is defined in `Result.h` (currently 6). The accumulator MUST use
that same constant so the array size (`1 << MAX_PLAYERS == 64`) cannot drift.
Include `Result.h`; do not redefine the constant.

**Marked seam:** the per-thread accumulator + `merge` is exactly the structure a
2-player precomputed lookup table (v1.2) writes its cached sub-results into.
No v1.0 work; noted so the struct isn't "simplified" in a way that blocks it.

### Decision 4 — The weight-normalization contract (correctness, NOT deferred)

This is a v1.0 correctness requirement, not an optimization. Getting it wrong
produces silently wrong equities that *pass* unweighted tests (where all weights
are 1.0) and only fail on weighted ranges.

**The rule:** the equity denominator is the **sum of the weights of scenarios
actually scored** — accumulated leaf by leaf, in lockstep with the numerator,
inside the one scoring function every scenario passes through. It is *never*:

- a scenario *count* (wrong the moment any weight ≠ 1.0), nor
- a *precomputed product* of range total-weights (wrong because inter-player
  conflicts skip scenarios whose weight would have been included in the product
  but never enters the tally).

**Enforcement is structural:** the numerator update and the denominator update
are adjacent lines inside `scoreScenario` (below). No caller can tally a
scenario without its weight entering the denominator, because both live in the
function and every leaf — exact or MC — routes through it. Conflicts are
rejected *before* `scoreScenario` is called, so they touch neither field.

This single function and contract serve both modes. In exact mode the weight is
the matchup's combo-weight product; in MC mode it is `1.0`. Either way
`scoreScenario` adds whatever it is handed to both fields, so the contract is
mode-agnostic and the MC denominator naturally becomes "number of samples
scored" — exactly right for a flat-tally estimator.

---

## Setup pipeline (runs once, before any driver)

Order matters here; several later invariants depend on it.

1. **Remove board/dead conflicts from each range.** Call
   `range.removeConflicts(boardMask | deadMask)` on every range. This drops any
   combo overlapping a community or dead card and keeps `totalWeight()`
   consistent (`Range.h` lines 26-27, 33). After this step, each range contains
   only combos that *could* be dealt given the fixed board.

   - If any range becomes empty here, the calculation is impossible (a player's
     entire range conflicts with the board). Define behavior: return a `Result`
     flagged empty / report an error. Do not divide by zero downstream.

2. **Snapshot range sizes.** Fill `rangeSizes_[i] = ranges_[i].size()` (post
   step 1). These are the mixed-radix bases for `decodeMatchup` and the factors
   of `N`. (Decision 1's `libdivide` seam later replaces this array's type.)

3. **Compute the preflop matchup count** `N = ∏ rangeSizes_[i]`. Guard against
   overflow for many large ranges (use `uint64_t`, saturate / branch to MC if it
   would overflow the threshold anyway).

4. **Compute the per-matchup runout count.** `remaining = 5 - board_.count()`;
   the runout count for a *typical* matchup is `C(deckSize, remaining)` where
   `deckSize = 52 - 2*n - board_.count()`. (Exact deck size varies slightly per
   matchup because hole cards differ, but this estimate is what mode selection
   uses.)

5. **Select mode.** `totalScenarios ≈ N * runoutCount`. If
   `totalScenarios < cfg_.enumThreshold` → exact (`enumerate`); else → Monte
   Carlo (`simulate`). Player count is not consulted directly; it falls out of
   the product. (Preserves the existing `CalculatorConfig` semantics.)

6. **(MC only) Build the weighted samplers.** For each range, build a
   cumulative-weight array from its **post-step-1** combos (see "Monte Carlo
   driver"). Must be built after `removeConflicts` so the sampler never draws a
   filtered combo and the cumulative total equals `totalWeight()`.

---

## The shared scoring function (convergence point)

Every scenario in both modes passes through exactly this:

```cpp
// playerHands[i] = the omp::Hand for player i's chosen combo (combo.evalHand).
// board          = a COMPLETE 5-card omp::Hand accumulator.
// weight         = scenario weight (exact: product of combo weights; MC: 1.0).
void scoreScenario(const omp::Hand* playerHands, unsigned n,
                   const omp::Hand& board, double weight,
                   BatchAccumulator& acc) const
{
    uint16_t best       = 0;
    uint32_t winnerMask = 0;

    for (unsigned i = 0; i < n; ++i) {
        uint16_t rank = eval_.evaluate(playerHands[i] + board);  // SSE add + lookup
        if (rank > best) {           // new sole leader
            best       = rank;
            winnerMask = (1u << i);
        } else if (rank == best) {   // tie for the lead
            winnerMask |= (1u << i);
        }
    }

    // === THE CONTRACT (Decision 4): both updates, adjacent, unbypassable ===
    acc.winsByPlayerMask[winnerMask] += weight;   // numerator
    acc.totalWeightTallied           += weight;   // denominator (lockstep)
    acc.scenariosScored              += 1;        // raw leaf count
}
```

Notes:
- `best` starts at 0; OMP strengths are ≥ 1 for any real hand, so the first
  player always becomes the leader cleanly.
- The `> / ==` split is what makes ties exact: a `k`-way chop yields a
  `winnerMask` with `k` bits set, and the tie-share split happens later in
  `buildResult`, not here.
- `eval_.evaluate` is the current single-signature call. The `tFlushPossible`
  optimization is deferred (see `06-evaluator.md`); do not template this call in
  v1.0. If added later, the flush-possible decision is computed **once per
  board** (it's a board property) and lives in the caller that owns the board,
  not inside this per-player loop.

---

## Exact driver — `enumerate()`

```
enumerate(acc):
    n = ranges_.size()
    for p in 0 .. N-1:                          # flat matchup index
        decodeMatchup(p, comboIdx[])            # Decision 1: %/÷ from rangeSizes_
        # gather combos + inter-player conflict check
        usedMask = board_.mask() | deadMask_
        weight   = 1.0
        ok = true
        for i in 0 .. n-1:
            combo = ranges_[i].combos()[comboIdx[i]]
            if combo.cardMask & usedMask:        # inter-player (or board) conflict
                ok = false; break
            usedMask |= combo.cardMask
            playerHands[i] = combo.evalHand
            weight *= combo.weight
        if not ok:
            continue                             # skip; touches NOTHING in acc
        runOutBoard(playerHands, n, usedMask, weight, acc)
```

```
runOutBoard(playerHands, n, usedMask, weight, acc):     # once-per-matchup setup
    # build deck ONCE (Decision 2)
    ndeck = 0
    for c in 0 .. 51:
        if not (usedMask & (1<<c)): deck[ndeck++] = c
    remaining = 5 - board_.count()
    if remaining == 0:                                  # board already complete
        scoreScenario(playerHands, n, board_.hand(), weight, acc); return
    recurseBoard(playerHands, n, board_.hand(), deck, ndeck, 0, remaining, weight, acc)
```

```
recurseBoard(playerHands, n, boardHand, deck, ndeck, start, remaining, weight, acc):
    if remaining == 0:
        scoreScenario(playerHands, n, boardHand, weight, acc); return
    for i in start .. ndeck-1:
        next = boardHand + omp::Hand(deck[i])           # one SSE add + table load
        recurseBoard(playerHands, n, next, deck, ndeck, i+1, remaining-1, weight, acc)
```

Properties:
- `weight` is the matchup's combo-weight product, threaded unchanged to every
  leaf of this matchup (all its runouts share the same matchup weight).
- The `start = i+1` advance gives each runout exactly once and never with a
  repeated card — this is combinations, not permutations, so each 5-card board
  is scored once.
- Conflicts (`continue`) never reach `scoreScenario`, so they never enter the
  denominator — Decision 4 satisfied by call-graph position.

---

## Monte Carlo driver — `simulate()`

Matchup selection uses **weighted sampling** (each combo drawn with probability
proportional to its weight); each drawn scenario then tallies a flat `1.0`. This
keeps every sample contributing equally (no weighted-estimator variance
inflation) and spends samples where they matter.

**Per-range sampler (built in setup step 6):** a cumulative-weight array.

```
buildSampler(range) -> cumulative[]:
    cumulative[0] = range.combos()[0].weight
    for k in 1 .. size-1:
        cumulative[k] = cumulative[k-1] + range.combos()[k].weight
    # cumulative[size-1] == range.totalWeight()
```

```
drawCombo(range, cumulative, rng) -> comboIdx:
    u = uniform_real(rng, 0, cumulative[size-1])    # [0, totalWeight)
    return lower_bound(cumulative, u)                # binary search -> index
```

```
simulate(acc):
    n = ranges_.size()
    remaining = 5 - board_.count()
    loop until stop condition:                       # see "Stopping"
        # sample matchup, retry on inter-player conflict
        usedMask = board_.mask() | deadMask_
        ok = true
        for i in 0 .. n-1:
            idx   = drawCombo(ranges_[i], cumulative_[i], rng)
            combo = ranges_[i].combos()[idx]
            if combo.cardMask & usedMask:
                ok = false; break
            usedMask |= combo.cardMask
            playerHands[i] = combo.evalHand
        if not ok:
            continue                                 # redraw; cheap

        # sample ONE runout: draw `remaining` distinct cards from the deck
        boardHand = board_.hand()
        m = usedMask
        for _ in 0 .. remaining-1:
            c = draw a random card in 0..51 not in m  # rejection or shuffled deck
            m |= (1<<c)
            boardHand = boardHand + omp::Hand(c)

        scoreScenario(playerHands, n, boardHand, 1.0, acc)   # weight = 1.0

        # periodic convergence check / batch flush (see Stopping + Threading)
```

Properties:
- Weight lives entirely in `drawCombo`; `scoreScenario` is handed `1.0`. The
  denominator therefore equals `scenariosScored`, the correct flat-estimator
  normalizer.
- Conflicts **retry** (redraw) rather than skip-and-count — a sample must be a
  valid matchup. Guard against pathological ranges where almost every draw
  conflicts (e.g. both players can only hold overlapping cards): cap retries and
  surface an error rather than spinning forever.
- The single-runout draw can use rejection sampling against `m`, or a
  partial Fisher–Yates over a scratch copy of the deck. Either is fine for v1.0;
  prefer whichever Claude Code can implement without bias (rejection sampling is
  simplest and unbiased).

**Marked seam (do NOT implement now):** replace the per-range cumulative array +
binary search with a Walker **alias table** (O(1) draws) if sampler profiling
ever shows it hot. Same interface (`drawCombo`), so it's a localized swap. At
poker range sizes (hundreds of combos) binary search is ~8-9 comparisons and
will not be the bottleneck; the alias table is deferred precisely because its
build algorithm is error-prone and a wrong sampler biases equities silently.

**Stopping.** MC runs until the per-player equity standard deviation drops below
`cfg_.mcStdevTarget`, or an iteration cap is hit. Compute a running stdev from
the accumulator at periodic batch boundaries (e.g. every few thousand
scenarios), not every iteration. Record the confidence-interval half-width into
`Result.ciHalfWidth`. (Exact mode leaves `ciHalfWidth = 0`.)

---

## Finalization — `buildResult()` (runs once, after the driver)

After the driver returns (or, later, after all threads' accumulators are merged
into one), derive `Result` from the single final accumulator in one pass.

```cpp
Result buildResult(const BatchAccumulator& acc, unsigned n,
                   double timeSeconds, Mode mode) const
{
    Result r;
    r.players = n;
    r.mode    = mode;

    for (unsigned mask = 1; mask < (1u << n); ++mask) {
        double w = acc.winsByPlayerMask[mask];
        if (w == 0.0) continue;
        unsigned k     = popcount(mask);     // players sharing this outcome
        double   share = w / k;              // chop: 1/k each
        for (unsigned i = 0; i < n; ++i) {
            if (mask & (1u << i)) {
                if (k == 1) r.wins[i] += w;        // outright win
                else        r.ties[i] += share;    // tie-share
                r.equity[i] += share;              // equity = wins + tie-shares
            }
        }
    }

    if (acc.totalWeightTallied > 0.0)
        for (unsigned i = 0; i < n; ++i)
            r.equity[i] /= acc.totalWeightTallied;   // Decision 4 normalization

    r.totalWeightTallied = acc.totalWeightTallied;
    r.totalScenarios     = acc.scenariosScored;
    r.timeSeconds        = timeSeconds;
    std::memcpy(r.winsByPlayerMask, acc.winsByPlayerMask,
                sizeof r.winsByPlayerMask);
    // r.ciHalfWidth set by the MC driver; 0 for exact.
    return r;
}
```

Properties:
- This is the **only** place division by the denominator happens.
- `equity[]` sums to ~1.0 across players because every scored scenario put its
  full `weight` into the denominator once and distributed exactly that weight
  across its winner set.
- `wins[]` / `ties[]` / `equity[]` are *derived*, never accumulated per leaf —
  one pass over the 64-entry tally at the end, not millions of per-player
  updates in the hot loop.

---

## Public surface (preserve existing `Calculator.h` contract)

Keep the existing public interface and config:

```cpp
struct CalculatorConfig {           // unchanged
    unsigned threadCount{1};
    double   mcStdevTarget{5e-5};
    uint64_t enumThreshold{50'000'000};
};

class Calculator {
public:
    explicit Calculator(std::vector<Range> ranges,
                        Board              board    = Board{},
                        uint64_t           deadMask = 0,
                        CalculatorConfig   cfg      = {});
    Result compute();                // setup -> select mode -> run -> buildResult
private:
    // drivers
    Result computeExact();           // wraps enumerate() + buildResult()
    Result computeMonteCarlo();      // wraps simulate()  + buildResult()
    // ... see "Private members" below
};
```

`compute()` runs the setup pipeline, selects the mode, dispatches to the matching
driver, and returns the finalized `Result`.

### Private members

```cpp
std::vector<Range>    ranges_;        // post-removeConflicts after setup
Board                 board_;
uint64_t              deadMask_;
CalculatorConfig      cfg_;
Evaluator             eval_;          // single shared instance (const evaluate)

uint64_t              rangeSizes_[MAX_PLAYERS];   // mixed-radix bases (Decision 1)
                                                  //  -> libdivide seam later
uint64_t              N_ = 0;                     // total matchups = product
// MC only:
std::vector<double>   cumulative_[MAX_PLAYERS];   // per-range samplers (Decision/MC)
```

> The previous `evaluateShowdown(..., double* tally)` and
> `buildResult(const double* tally, ...)` signatures are **replaced** by
> `scoreScenario(..., BatchAccumulator&)` and
> `buildResult(const BatchAccumulator&, ...)`. The bare-`double*`-tally design is
> retired in favor of the accumulator struct (Decision 3) so the
> tally/denominator/count travel together and merge cleanly.

---

## Invariants

- **Normalization:** `equity[i] = (weighted wins + weighted tie-shares) /
  totalWeightTallied`, where `totalWeightTallied` is accumulated leaf-by-leaf in
  `scoreScenario`. Never divide by scenario count or a precomputed product.
- **Conflict accounting:** a skipped matchup contributes to *no* accumulator
  field. Conflicts are rejected before `scoreScenario`.
- **Determinism (exact):** output is independent of matchup iteration order and,
  later, of thread count — guaranteed by additive, commutative `merge`.
- **One write per leaf:** scoring does a single `winsByPlayerMask[mask] +=
  weight` plus the two contract lines; no per-player branching at score time.
- **Deck integrity:** `deck[]` holds only unused card indices; `start=i+1` makes
  runouts combinations (each board once), and the `omp::Hand` no-overlap assert
  holds automatically.
- **Shared evaluator:** one `eval_`, `const`, called from every scenario (and,
  later, every thread). Never construct an evaluator in the loop.
- **`MAX_PLAYERS` single source:** the tally array size derives from
  `Result.h`'s `MAX_PLAYERS`; never redefine it.

---

## Hard parts (where Claude Code tends to go wrong — check these)

1. **Denominator drift (the #1 bug).** If the generated code computes the
   denominator as `∏ range.totalWeight()` or as `scenariosScored` in exact mode,
   it is WRONG for weighted ranges and will still pass `AA vs KK` (all weights
   1.0). The denominator MUST be `acc.totalWeightTallied`, accumulated inside
   `scoreScenario`. Verify the two contract lines are adjacent and that no caller
   computes its own denominator.

2. **Rebuilding the deck inside the recursion.** The deck must be built once in
   `runOutBoard`, not inside `recurseBoard`. If `recurseBoard` scans 0-51 or
   reconstructs available cards per node, that's the inefficiency Decision 2
   exists to prevent.

3. **Calling `Board::withCard()` in the hot loop.** The recursion works on
   `omp::Hand`, not `Board`. `withCard()` is for fixed-board/test construction
   only. If the generated recursion passes `Board` and calls `withCard` per
   node, correct it to `omp::Hand` + `operator+`.

4. **Permutations instead of combinations in the runout.** The `start=i+1`
   advance is mandatory. Starting each level from 0 scores every board in
   multiple card orders, inflating counts and corrupting equities.

5. **MC: skipping instead of retrying on conflict.** A Monte Carlo sample must be
   a valid matchup. On conflict, redraw — do not score a partial scenario and do
   not count the skip toward the denominator.

6. **MC weighting in the wrong place.** With weighted sampling, the weight is in
   `drawCombo`; `scoreScenario` receives `1.0`. Do NOT also multiply combo
   weights into the MC scenario weight — that would double-apply the weighting.

7. **Constructing the evaluator per thread / per loop.** One shared `const`
   `eval_`. See `06-evaluator.md`.

8. **Premature optimization.** No `libdivide`, no suit isomorphism
   (`suitCounts[]`), no alias table, no 2-player lookup table, no
   `tFlushPossible=false` in v1.0. Each has a marked seam above. Implement the
   simple correct form; leave the seam comments.

---

## Test cases to expect

- **Canonical equity:** `AA` vs `KK` preflop, exact → ~81.9% / ~18.1%. The
  single most important regression test.
- **Symmetry:** identical ranges for two players → 50/50 (within MC tolerance).
- **Known multiway:** a documented 3-way preflop spot → published equities.
- **Tie handling:** force a guaranteed chop (e.g. both players play the board on
  a complete board that out-ranks both holdings) → equity split 50/50 via
  `ties[]`, and `winsByPlayerMask` has the correct 2-bit entry.
- **Weighted normalization (guards bug #1):** a range like `AA:1.0, KK:0.5`
  against a fixed opponent. Hand-compute the expected equity using
  weight-summed normalization and assert it. This test MUST fail if the
  denominator is a count or a precomputed product. Include a variant where an
  inter-player conflict removes a scenario, so the precomputed-product
  denominator would diverge from the correct one.
- **Board/dead removal:** a range whose some combos conflict with the fixed
  board → those combos absent from enumeration; `totalWeight()` consistent;
  empty-range case handled.
- **Mode parity:** a spot small enough for exact, also run under MC with a tight
  `mcStdevTarget` → MC equity within CI of the exact answer. Validates that both
  drivers feed the accumulator consistently.
- **Mode selection:** a tiny spot picks exact; a huge spot
  (`> enumThreshold`) picks MC. Assert the chosen mode in `Result.mode`.
- **Determinism (exact):** running the same exact spot twice yields
  bit-identical `Result` (and, once threaded, identical across thread counts).
- **Complete-board shortcut:** a spot with a full 5-card fixed board → no runout
  recursion, one scenario per valid matchup.

---

## OMP interaction

- The recursion combines `omp::Hand` via `operator+` and constructs single-card
  hands via `omp::Hand(cardIdx)` (table load). `cardIdx` is `Card.value` (0-51),
  whose encoding (`rank*4+suit`) already matches OMP's card index (`Card.h`).
- Scoring calls `eval_.evaluate(playerHands[i] + board)`.
- The calculator touches `omp::Hand` directly only in the runout recursion and
  scoring; this is the documented, expected boundary crossing (see
  `00-overview.md`).
- No other OMP types are used here; `Range`/`Combo`/`Board` already encapsulate
  the rest.

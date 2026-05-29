# Calculator

## Purpose

The Calculator is the orchestrator and the hardest class (~600 lines across enumeration, Monte Carlo, and threading). It takes the ranges, board, and dead cards; decides enumeration vs MC by **scenario count**; runs the loops; tallies winners with **weights**; and returns a `Result`. Everything else in the engine exists to feed this loop.

## Data members

- `std::vector<Range> ranges_` — one per player (2–6).
- `Board board_` — fixed board cards (may be empty/partial/complete).
- `uint64_t deadCards_` — dead-card mask.
- `Evaluator& eval_` — shared, const, one instance (see Evaluator doc).
- Settings: `unsigned threadCount_`, `double mcStdevTarget_`, `uint64_t enumThreshold_`, optional time/hand limits.
- Shared tally state for threading: a mutex + a merged accumulator, or per-thread accumulators merged at the end (preferred — see Threading).

## Methods

- `Result compute()` — entry point; removes conflicts, picks mode, runs it, builds the Result.
- `Result computeExact()` — full enumeration.
- `Result computeMonteCarlo()` — sampled.
- `uint64_t getPreflopCombinationCount()` — product of range sizes.
- `uint64_t getPostflopCombinationCount()` — `C(deckRemaining, boardCardsToCome)`.
- `void evaluateShowdown(playerHands, board, weight, tally)` — score one completed scenario, tally the winner(s).
- board-enumeration helpers (recursive or nested loops).

## The hard parts (this is where the depth lives)

### 1. Mode selection by scenario count

The decision you asked for — drive on estimated total scenarios, not player count.

```
totalScenarios = getPreflopCombinationCount() * getPostflopCombinationCount()
if totalScenarios < enumThreshold_:  computeExact()
else:                                computeMonteCarlo()
```

- `getPreflopCombinationCount` = product of each range's `size()` (a pre-conflict estimate; good enough for the decision).
- `getPostflopCombinationCount` = n-choose-k where n = cards left in the deck (52 − used) and k = board cards still to come (5 − `board_.count()`).

Player count falls out: 6 players almost always exceeds the threshold → MC; a 6-player all-specific-hands river spot (k=0, tiny product) → enumerate. Both reference implementations live in OMP's `EquityCalculator.cpp` (`getPreflopCombinationCount`, `getPostflopCombinationCount`).

### 2. The showdown evaluation

Given N players' hole hands (`omp::Hand`) and a complete board (`omp::Hand`):

```
evaluateShowdown(hands[], n, board, weight, tally):
    bestRank = 0
    winnersMask = 0
    for i in 0..n-1:
        seven = hands[i] + board        // SSE add (cached combo hand + board hand)
        rank  = eval_.evaluate(seven)   // ~5ns
        if rank > bestRank:
            bestRank = rank
            winnersMask = (1 << i)
        else if rank == bestRank:
            winnersMask |= (1 << i)
    tally[winnersMask] += weight
```

The `hands[i]` are the **pre-built** `combo.evalHand`s — never rebuilt here. The combine is one SSE instruction; the evaluate is the perfect-hash lookup. This inner body is what runs hundreds of millions of times, and it's the same work OMP does.

### 3. The `winsByPlayerMask` tally trick (non-obvious — read carefully)

Rather than separate win/tie counters per player, accumulate into an array indexed by the **bitmask of who won that scenario**. Single winner = player 0 → mask `0b001`. Two-way tie between players 0 and 1 → mask `0b011`. The array has `2^n` entries (≤ 64 for 6 players).

After accumulation, decompose:

```
buildResult():
    for mask in 0 .. (1<<n)-1:
        if tally[mask] == 0: continue
        winnerCount = popcount(mask)
        for each player i with bit set in mask:
            if winnerCount == 1: wins[i]  += tally[mask]
            else:                ties[i]  += tally[mask] / winnerCount
    for i in 0..n-1:
        equity[i] = (wins[i] + ties[i]) / totalWeight
```

Why this is better than per-player counters: it's a single add per scenario (index by mask, `+= weight`) instead of N conditional updates, and it preserves the full joint outcome (you know exactly how often players 0 and 2 chopped while 1 lost — useful for multiway analysis and v1.1). This is OMP's approach; it's worth understanding rather than reinventing per-player counters.

### 4. Cartesian product + inline conflict skip

The outer loop is the cartesian product of each range's combos. Skip pairings that share a card:

```
for c0 in range[0].combos:
  for c1 in range[1].combos:
    if c0.cardMask & c1.cardMask: continue        // conflict
    for c2 in range[2].combos:                    // ... up to n players
      if (c0|c1).cardMask & c2.cardMask: continue
      ...
        usedMask = board_.mask | deadCards_ | all combos' masks
        enumerate boards over the remaining deck
```

For v1.0 the conflict check is **inline** (one AND + compare, ~1ns). The `CombinedRange` precomputation that OMP uses is a v1.5 optimization for heavily-overlapping multi-player MC — not now. Don't let Claude Code build CombinedRange for v1.0; it's ~200 lines that buys nothing for the 2–3 player case.

### 5. Board enumeration

For partial boards, enumerate the remaining `5 - count` cards from the cards not already used:

```
build deck = all cards not in usedMask
recursively choose (5 - board.count()) cards from deck:
    at each level, board' = board.withCard(deck[i]); recurse from i+1
    at the bottom (board complete): evaluateShowdown(...)
```

The `start = i+1` index prevents choosing the same card twice / counting a board in multiple orders. For v1.0 this is the naive recursion. Suit isomorphism (OMP's `enumerateBoardRec` "irrelevant suit" trick, ~3× speedup) is v1.2 — note it, don't build it yet.

### 6. Weights in the tally and normalization

Each scenario contributes `weight_0 * weight_1 * ... * weight_{n-1}` to `tally[winnersMask]`, not `1`. Equity normalizes by the **sum of scenario weights actually tallied** (equivalently, for independent ranges, the product of range total-weights minus conflict adjustments), not by scenario count.

The safe, always-correct approach: accumulate `totalWeightTallied += scenarioWeight` alongside the tally, then `equity[i] = (wins[i] + ties[i]) / totalWeightTallied`. This avoids subtle errors from conflict removal changing the effective denominator. If Claude Code divides by scenario *count* instead of weight, weighted ranges produce wrong equities — check this.

### 7. Threading

Parallelize the outer space (matchups for enumeration; sample streams for MC). Each thread keeps a **local tally** (its own `winsByPlayerMask` array, padded to a cache line — 64 bytes — to avoid false sharing). No locks in the hot path. At the end, join and sum the local tallies into the global one under a single lock (or just sequentially after join).

For enumeration: a shared atomic counter hands out batches of the matchup index space (`reserveBatch`-style), so threads grab disjoint chunks without coordinating per-iteration. For MC: each thread runs independent samples with its **own RNG** (seeded differently); the shared `Evaluator` is fine because it's const.

OMP's `reserveBatch` and per-thread `BatchResults` are the reference. The cache-line padding matters: two threads writing adjacent memory thrash each other's caches (false sharing) and can erase the benefit of threading entirely.

### 8. Monte Carlo convergence

MC samples random scenarios until the answer stops moving. Track running mean and variance of player-0 equity across batches; stop when the standard deviation (CI half-width proxy) drops below `mcStdevTarget_`, or a hand/time limit is hit.

```
each batch: compute batch equity; accumulate sum, sumSq, count
stdev = sqrt(sumSq - sum*sum/count) / count     // (schematically)
stop when stdev < target
```

OMP's `updateResults` does exactly this; read it for the exact accumulation. The default target (~5e-5 in OMP, or your chosen CI like 0.5% half-width) controls runtime vs precision.

### 9. Equity computation

`equity[i] = (wins[i] + tieShareForPlayer i) / totalWeightTallied`, where a k-way tie contributes `scenarioWeight / k` to each tied player. This is the decomposition in part 3. Equities across players sum to ~1.0 (the tie shares conserve total weight).

## Invariants

- Conflicts are removed from ranges (against board + dead) before counting/looping.
- The same `Evaluator` instance is shared by all threads; it is never mutated.
- Per-thread tallies are merged exactly once, after all threads join.
- `equity[i]` sums to approximately 1.0 across players (within MC noise for sampled mode; exactly for enumeration).
- Enumeration is deterministic; MC is seeded per-thread and reproducible only if seeds are fixed.

## OMP interaction

Indirect. `calculator.cpp` works with `omp::Hand` only through `Combo::evalHand` and `Board::hand()`, and scores only through `Evaluator`. It includes `<omp/Hand.h>` transitively (via combo.h/board.h) and calls `operator+` on `omp::Hand`, but it does not include `HandEvaluator.h` directly — that's behind `Evaluator`. Read OMP's `EquityCalculator.cpp` for the enumeration/MC/threading reference, then write your own (simpler, single-threaded first, weights added).

## Pseudocode (top level)

```
compute():
    for each range: range.removeConflicts(board_.mask | deadCards_)
    total = getPreflopCombinationCount() * getPostflopCombinationCount()
    if total < enumThreshold_: return computeExact()
    else:                      return computeMonteCarlo()

computeExact():
    spawn threadCount_ workers; each pulls batches of the matchup index space
    per worker, local tally:
        for each conflict-free combo assignment in its batch:
            enumerate board completions:
                evaluateShowdown(hands, board, weightProduct, localTally)
    merge local tallies -> global
    return buildResult(global)

computeMonteCarlo():
    spawn workers; each with own RNG, local tally:
        loop:
            sample one conflict-free combo per player (rejection sample)
            sample the remaining board cards
            evaluateShowdown(...)
            periodically report batch equity for convergence check
            stop when stdev < target or limit hit
    merge; return buildResult(global)
```

## Test cases to expect

- AA vs KK preflop ≈ 81.9% / 18.1% (known canonical value)
- AA vs KK vs QQ three-way (known multiway value)
- Hand vs hand on a complete board → deterministic single scenario, correct winner
- Identical hands → 50/50 tie split
- Weighted range shifts equity in the correct direction (e.g. weighting toward strong combos raises equity)
- Enumeration and MC agree within MC tolerance on the same spot
- Dead cards change the result (e.g. removing an ace from the deck changes AA-vs-random)
- Mode selection: a tiny spot enumerates; a huge preflop range-vs-range selects MC
- Equities sum to ~1.0
- Threaded and single-threaded runs produce the same enumeration result

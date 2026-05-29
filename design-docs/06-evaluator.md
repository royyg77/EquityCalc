# Evaluator

## Purpose

`Evaluator` is a thin wrapper around `omp::HandEvaluator`. It takes a built `omp::Hand` and returns a strength number (higher = stronger). It's the single point where actual hand scoring happens — the "coral box" in the data-flow diagram. ~50 lines, but it's the primary OMP boundary and has two subtle concerns: object lifetime and thread-safety.

## Data members

- `omp::HandEvaluator eval_` — holds the (static) lookup tables. Constructed once, reused for every evaluation.

## Constructors / lifecycle

- `Evaluator()` — constructs the `omp::HandEvaluator`. **This triggers a one-time, thread-safe static initialization** of OMP's large lookup tables (the perfect-hash table and flush table). The first `Evaluator` ever constructed pays the init cost; subsequent ones are cheap because the tables are static. See `HandEvaluator.cpp`'s `cardInit` / `staticInit` pattern.

Practical rule: construct **one** `Evaluator`, hold it in the `Calculator`, share it. Do not construct one per evaluation or per thread (see Thread-safety).

## Methods

- `uint16_t evaluate(const omp::Hand& hand) const` — delegates to `eval_.evaluate(hand)`. Returns a 16-bit strength.
- `static HandCategory category(uint16_t strength)` — `strength >> 12` (i.e. `/ 4096`) gives the hand category (1 = high card, 2 = pair, … 9 = straight flush). Useful for v1.1 made-hand breakdowns.
- (optional convenience) `uint16_t evaluate(const Combo& c, const Board& b) const` — combines `c.evalHand + b.hand()` then evaluates. Keeps the combine+score in one place.

## Invariants

- `eval_` is fully initialized before any `evaluate` call (guaranteed by construction).
- `evaluate` is `const` and read-only: it mutates no state. This is what makes the Evaluator safely shareable across threads.
- Strength ordering is total and monotone: a stronger poker hand always returns a strictly larger number; equal-strength hands return equal numbers. (This is OMP's guarantee; your wrapper just passes it through.)

## Hard parts

**Lifetime / one-time init.** `omp::HandEvaluator`'s constructor runs a guarded one-time static init of multi-kilobyte lookup tables. The cost is paid once per process, but only if you don't thrash construction. The mistake to avoid: constructing an `Evaluator` (or a bare `omp::HandEvaluator`) inside the hot loop, or per matchup. Construct it once at Calculator setup. If Claude Code instantiates evaluators inside loops, correct it.

**Thread-safety — share one, don't clone per thread.** Because `evaluate` is const and the tables are static and read-only after init, a single `Evaluator` is safe to call from many threads simultaneously. The threading model therefore shares one `Evaluator` across all worker threads — you do **not** need (and should not make) one per thread. This is a common misconception worth checking: if the generated threading code gives each thread its own `Evaluator`, it's wasteful (extra construction) though not incorrect; if it gives each thread its own and they somehow share mutable state, that's a bug. The right design: one shared const `Evaluator`, called concurrently.

**The `tFlushPossible` template parameter (defer).** OMP's `evaluate<bool tFlushPossible>` takes a compile-time flag; passing `false` skips the flush check when the caller knows no flush is possible (certain board textures), for a small speedup. For v1.0, ignore it — always use the default (`true`). Note it exists so you can add the optimization in v1.2 alongside suit isomorphism. Don't let Claude Code prematurely wire up the `false` path; it requires the caller to correctly prove no-flush, which is its own logic.

**Return-value encoding is useful later.** The 16-bit strength encodes category in the high bits (`/ 4096`). v1.1's "how often does this range flop a flush / two pair / etc." breakdowns read exactly this. Expose `category()` now even though v1.0 doesn't use it much — it's free and it documents the encoding.

## Pseudocode

```
Evaluator():
    // constructing eval_ triggers OMP's one-time static table init

evaluate(hand) -> uint16_t:
    return eval_.evaluate(hand)

category(strength) -> HandCategory:
    return HandCategory(strength >> 12)     // / 4096
```

## OMP interaction

- `evaluator.h` includes `<omp/HandEvaluator.h>`.
- Holds an `omp::HandEvaluator` member.
- Calls `eval_.evaluate(hand)`.
- The primary of the three OMP-boundary files.

## Test cases to expect

- Ordering sanity (the canonical test): royal flush > straight flush > quads > full house > flush > straight > trips > two pair > pair > high card. Build representative 7-card hands and assert the strict ordering.
- `category()` extraction: a known flush hand → category 6; a known pair → category 2.
- Determinism: the same hand evaluated twice returns the same number.
- 5-, 6-, and 7-card hands all evaluate without assertion failure (OMP supports up to 7).
- Concurrency smoke test: many threads calling `evaluate` on a shared `Evaluator` produce correct, consistent results (no data race).

# Result

## Purpose

`Result` is the output of one calculation: per-player equity, wins, and ties, plus metadata (scenarios evaluated, time, mode, and for MC, the uncertainty). Plain data with a `format()` method. ~50 lines. No OMP, no logic beyond presentation.

## Data members

- `unsigned players` — number of players (2–6).
- `double equity[MAX_PLAYERS]` — equity per player, 0–1.
- `uint64_t wins[MAX_PLAYERS]` — outright wins per player (weighted count).
- `double ties[MAX_PLAYERS]` — tie equity per player (fractional, e.g. half a scenario for a two-way chop).
- `uint64_t totalScenarios` — scenarios evaluated (or effective weighted count).
- `double timeSeconds` — wall-clock duration.
- `enum class Mode { Enumeration, MonteCarlo }` — which path ran.
- `double ciHalfWidth` — confidence-interval half-width for MC (0 / unused for enumeration).
- (optional) `uint64_t winsByPlayerMask[1 << MAX_PLAYERS]` — the full joint outcome breakdown, for multiway analysis and v1.1.

`wins` is `uint64_t` for enumeration (exact counts) but holds weighted sums; if weights are fractional you may prefer `double`. Decide based on whether you allow non-integer weights in v1.0. Recommendation: `double` for both `wins` and `ties` once weights exist, since a "win" can be a fractional weighted contribution.

## Constructors / lifecycle

- Default-constructed zeroed, then filled by `Calculator::buildResult()`. It's a passive carrier — created once at the end of a calculation, read by the CLI, then discarded.

## Methods

- `std::string format() const` — human-readable columnar output.
- `std::string toJson() const` — machine-readable, for the `--json` flag.

## Invariants

- `equity[i]` for `i < players` sums to approximately 1.0 (exactly for enumeration; within MC noise for sampled).
- For enumeration, `ciHalfWidth == 0` and the answer is exact.
- For MC, `ciHalfWidth > 0` and reflects the convergence target that was hit.
- `wins[i] + ties[i]` over total weight equals `equity[i]` (consistency between the raw tallies and the normalized equity).

## Hard parts

**Equity vs raw counts with weights.** `equity[i]` is `(wins[i] + ties[i]) / totalWeightTallied`, computed by the Calculator. `Result` just stores and displays it. The subtlety is only at computation time (Calculator doc, parts 6 and 9); by the time data reaches `Result`, the normalization is done. Don't recompute equity in `format()` — display the value the Calculator put there.

**MC carries uncertainty; enumeration doesn't.** An enumeration result is exact: "46.20%". An MC result is an estimate: "46.2% ± 0.4%". `format()` should show the `± ciHalfWidth` only in MC mode, and label the mode. Conflating the two (showing a spuriously exact percentage for an MC run) misleads the user about precision. This is the main presentation decision.

**The joint breakdown (optional, for multiway).** `winsByPlayerMask` preserves how often each *set* of players won together. For heads-up it's redundant with `wins`/`ties`, but for 3+ players it answers "how often did players 1 and 3 chop?" — valuable for analysis and a natural hook for v1.1. Store it if cheap (it's already computed in the Calculator's tally); display it only on request (a `--verbose` flag), since the full table is `2^n` rows.

**Number formatting.** Percentages to 2 decimals, counts with thousands separators, time in seconds with sensible precision. Minor, but it's the part the user actually reads. Keep `format()` and `toJson()` consistent in the values they report (same equities, just different presentation).

## Pseudocode

```
format():
    lines = []
    for i in 0..players-1:
        line = "P{i+1}: {equity[i]*100:6.2f}%   wins: {wins[i]}   ties: {ties[i]:.1f}"
        lines.add(line)
    footer = "{totalScenarios} scenarios in {timeSeconds:.2f}s ({mode})"
    if mode == MonteCarlo:
        footer += "  (±{ciHalfWidth*100:.2f}%)"
    return join(lines, "\n") + "\n" + footer

toJson():
    return JSON object with players, equity[], wins[], ties[],
           totalScenarios, timeSeconds, mode, ciHalfWidth
```

## OMP interaction

None. `result.h` names no OMP type. Plain struct + formatting.

## Test cases to expect

- `format()` produces aligned columns with the expected fields
- Equities sum to ~1.0 across `players`
- MC result includes the `±` uncertainty; enumeration result does not
- `toJson()` round-trips (parse back to the same numbers)
- Consistency: `(wins[i] + ties[i]) / totalWeightTallied == equity[i]` within float tolerance
- A two-way tie shows as 0.5 contribution to each tied player's `ties`

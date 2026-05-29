# Range

## Purpose

`Range` is one player's set of possible hole-card combos, with weights. It's a container — mostly data, little behavior. The parser builds it; the Calculator consumes it. Conceptually, a `Range` is "everything player N might be holding, and how likely each holding is (the weight)."

After parsing, abstractions like "QQ" no longer exist — only the concrete `Combo`s they expanded into. A `Range` is a flat list of weighted `Combo`s, period. Internalizing that removes most range-related confusion.

## Data members

- `std::vector<Combo> combos_` — sorted (canonical order) and deduplicated.

That's the whole state. Everything else is derived.

## Constructors / lifecycle

- `Range()` — empty.
- Built up by the parser via `add()`, then `normalize()` once at the end.
- Also constructible from a `std::vector<Combo>` for tests.

Lifecycle: parser creates it, fills it, normalizes it, hands it to the Calculator. After `removeConflicts()` is applied (once board/dead cards are known) it's read-only for the rest of the calculation.

## Methods

- `void add(Combo)` — append. Dedup/sort deferred to `normalize()` for efficiency (don't re-sort on every add).
- `void normalize()` — sort by canonical order, then dedup applying the weight policy.
- `const std::vector<Combo>& combos() const`
- `size_t size() const`
- `double totalWeight() const` — sum of all combo weights. Needed for equity normalization, not `size()`.
- `void removeConflicts(uint64_t deadMask)` — erase combos whose `cardMask` overlaps the board/dead-card mask.

## Invariants

- After `normalize()`: `combos_` is sorted in canonical order and contains no two combos with the same pair of cards.
- Every contained `Combo` is valid (two distinct cards, correct mask, built `evalHand`).
- `totalWeight()` reflects the current contents (recomputed or maintained consistently after `removeConflicts`).

## Hard parts

**Dedup with weights — the policy decision.** OMP's `CardRange` dedups purely by cards because it has no weights. We do have weights, so "QQ:0.5,QhQd:1.0" is a genuine conflict: the QhQd combo is specified twice with different weights. You must pick a policy and document it:

- **Last-write-wins:** the later specification overrides the earlier. Intuitive for users building ranges incrementally. Recommended.
- **Max:** take the higher weight.
- **Error:** reject conflicting weights. Strict but noisy.

Recommendation: last-write-wins, documented in both the code and the user-facing help. Whatever you pick, the dedup in `normalize()` must implement it deliberately — if Claude Code just calls `std::unique` (which keeps the first, not the last, and ignores weight), that silently picks an undocumented policy. Check this.

**`removeConflicts` timing and meaning.** When board/dead cards are known, any combo using one of those cards is impossible and must be dropped before the calculation — otherwise the cartesian loop wastes time on combos that always conflict, and (worse) the equity denominator is wrong. This is a one-time setup step. It's the reason `Combo` carries a `cardMask`: the check is `(combo.cardMask & deadMask) != 0`, one instruction per combo. After removal, recompute `totalWeight()`.

**`totalWeight()` vs `size()` for normalization.** Equity is normalized by weight, not combo count. A range "AA:1.0, KK:0.5" has 12 combos but total weight 6×1.0 + 6×0.5 = 9.0. The Calculator divides by weight products, not combo counts. If you accidentally normalize by `size()`, weighted ranges produce wrong equities. The Calculator doc covers the math; Range's job is to expose `totalWeight()` correctly.

**Sorting reuses Card ordering.** Canonical order is: first card rank, second card rank, first card suit, second card suit. Because `Card`'s `operator<` is rank-major on `value`, the `Combo` comparator composes cleanly from card comparisons. No bespoke bit-twiddling needed.

## OMP interaction

None directly. `range.h`/`range.cpp` name no OMP type and call no OMP function. (They include `combo.h`, which transitively pulls in `<omp/Hand.h>`, but Range code never touches `omp::Hand` itself.) This is a "transitively includes" relationship, not a boundary — Range is your code through and through.

## Pseudocode

```
add(combo):
    combos_.push_back(combo)        // no sort yet

normalize():
    sort(combos_)                   // canonical order via Combo::operator<
    // dedup with last-write-wins on weight:
    walk sorted combos_; when two adjacent have the same cards,
        keep the later one's weight, drop the earlier
    erase the dropped entries

removeConflicts(deadMask):
    erase_if(combos_, [&](c){ return c.cardMask & deadMask; })
    // recompute or invalidate cached totalWeight

totalWeight():
    return sum(c.weight for c in combos_)
```

## Test cases to expect

- Add 6 QQ combos → `size() == 6` after normalize
- Adding a duplicate combo → deduped to one
- Weight policy: parsing "QQ:0.5,QhQd:1.0" → QhQd ends with weight 1.0 (last-write-wins)
- `removeConflicts` with a board containing Qh removes the 3 QQ combos that contain Qh
- `totalWeight()` of "AA:1.0,KK:0.5" == 9.0
- Normalized order is canonical (stable, rank-major)
- Empty range: `size() == 0`, `totalWeight() == 0`

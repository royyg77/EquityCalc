# Combo

## Purpose

`Combo` is two specific cards plus a weight, plus the cached data the hot loop needs. It's the first OMP boundary, and it's where the single most important performance decision in the project lives: **caching the pre-built `omp::Hand`** so the inner loop never rebuilds it. Get this right and your inner loop runs at OMP speed; get it wrong (rebuild per iteration) and you're 10–50× slower.

A `Combo` is one concrete two-card holding — e.g. Q♥Q♦. A range like "QQ" expands into six Combos.

## Data members

- `Card c1, c2` — the two cards, canonical order (higher rank first; see Invariants).
- `float weight` — default 1.0. Allows weighted ranges (`QQ:0.5`).
- `uint64_t cardMask` — bits set for `c1.value` and `c2.value`. Exactly two bits. Used for O(1) conflict detection.
- `omp::Hand evalHand` — the bit-packed hand pre-built from `c1` and `c2`. The optimization.

Why store both `Card`s **and** an `omp::Hand` for the same two cards — deliberate redundancy. The `Card`s are for identity, display, dedup, conflict masks (human/structural concerns). The `omp::Hand` is for the hot loop (compute concern). Same data, two representations, because they serve different masters. Without the cached `omp::Hand`, every inner-loop iteration would call `omp::Hand({c1.value, c2.value})` again — building it from OMP's `CARDS[]` table — hundreds of millions of times.

## Constructors / lifecycle

- `Combo(Card a, Card b, float w = 1.0)` — canonicalizes order, builds `cardMask`, builds `evalHand`. This is where the `omp::Hand` is constructed, once, at parse time.

Combos are created by the parser (during range expansion) and then live, unchanged, inside a `Range`'s vector for the duration of a calculation. They are read-only in the hot loop.

## Methods

- `bool conflicts(const Combo& other) const` → `(cardMask & other.cardMask) != 0`
- `bool conflicts(uint64_t usedMask) const` → `(cardMask & usedMask) != 0` (against board/dead cards)
- `bool operator==(const Combo&) const` → same two cards (see weight note below)
- `bool operator<(const Combo&) const` → canonical ordering for sort/dedup
- accessors for the cards / weight

## Invariants

- **Canonical order:** `c1 >= c2` by card value (higher card first). Established in the constructor by swapping if needed. This makes dedup work, keeps display consistent, and matches how `Range` sorts.
- `c1 != c2` — a combo can never hold the same card twice. Assert this in the constructor.
- `cardMask` has exactly 2 bits set, corresponding to `c1.value` and `c2.value`.
- `evalHand` is exactly `omp::Hand` built from `c1` and `c2`, and `evalHand.count() == 2`.
- `weight` is typically in `[0, 1]`. Decide whether to clamp or trust; document the choice. (Recommendation: trust the parser to produce sane weights, assert `weight >= 0` in debug.)

## Hard parts

**The `evalHand` cache is THE optimization.** Build the `omp::Hand` once in the constructor; reuse it for every board in every matchup. In the hot loop the combine is a single SSE instruction:

```
omp::Hand seven = combo.evalHand + board.evalHand;   // one _mm_add_epi32
uint16_t rank   = eval.evaluate(seven);              // perfect hash, ~5ns
```

This mirrors OMP's own `CombinedRange::Combo::evalHands`, which caches pre-built hands for the same reason. If Claude Code generates a `Combo` that only stores `Card`s and rebuilds the `omp::Hand` inside the loop, that is the single biggest performance drift possible — correct it immediately.

**Alignment.** `omp::Hand` contains a `__m128i`, which requires 16-byte alignment. A `std::vector<Combo>` must keep each `Combo` aligned. On x64, default allocation is already 16-byte aligned, so this is usually a non-issue. On 32-bit targets it segfaults without help — OMP ships `OMP_ALIGNED_STD_ALLOCATOR` for exactly this. Since you're almost certainly on x64, note it and move on, but if you ever see alignment-related crashes, this is the cause.

**Canonical ordering rationale.** Storing higher card first means "Q♥Q♦" and "Q♦Q♥" are the same Combo after construction, so dedup is a simple equality check and sorting produces a stable, human-sensible order. Without canonicalization you'd have to compare unordered pairs everywhere.

**Equality and weight.** Two Combos with the same cards but different weights — are they equal? For dedup purposes, "same cards" should collide (so a range can't list the same combo twice with conflicting weights). `operator==` should compare cards only; the weight-conflict resolution policy lives in `Range::normalize()` (see `03-range.md`). Document that `operator==` ignores weight.

## Pseudocode

```
Combo(Card a, Card b, float w):
    assert a.value != b.value
    if a < b: swap(a, b)              // canonical: higher card first
    c1 = a; c2 = b; weight = w
    cardMask = (1ull << c1.value) | (1ull << c2.value)
    evalHand = omp::Hand({c1.value, c2.value})   // built ONCE here
```

## OMP interaction

- `combo.h` includes `<omp/Hand.h>`.
- Constructs `omp::Hand({c1.value, c2.value})` in the constructor.
- Exposes `evalHand` so the Calculator can combine it with a board.
- One of the three files that directly touch OMP.

## Test cases to expect

- `Combo(Ah, Ks)` → `c1 == Ah`, `c2 == Ks` (already ordered)
- `Combo(Ks, Ah)` → canonicalizes to `c1 == Ah`, `c2 == Ks`
- `cardMask` has exactly 2 bits set
- `conflicts`: `Combo(Ah,Ks)` conflicts with `Combo(Ah,Qd)` (share Ah), not with `Combo(2c,3d)`
- `conflicts(mask)`: against a board mask containing Ah → true
- `weight` defaults to 1.0; `Combo(Ah,Ks,0.5).weight == 0.5`
- `evalHand.count() == 2`
- `operator==` ignores weight: `Combo(Ah,Ks,1.0) == Combo(Ah,Ks,0.5)` is true (same cards)
- Constructing `Combo(Ah, Ah)` asserts/fails (same card twice)

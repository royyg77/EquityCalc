# Board

## Purpose

`Board` represents the community cards (0, 3, 4, or 5 of them). It's the second OMP boundary. Per our decision, it stores an `omp::Hand` accumulator + a `cardMask` + a count — **no array of `Card`s, no sentinel.** This matches how OMP represents a board (as a `Hand`, via `count()`) and eliminates the "undealt slot" problem entirely.

## Why no Card array

An earlier draft had `Board` hold `std::array<Card,5>`, which forced the question "what's in the undealt slots?" and led to sentinel-value complications. OMP sidesteps this: it never stores a fixed array of optional cards. A board is a `Hand` whose `count()` tells you how many cards are in it; "empty" is a valid initialized `Hand`, not five empty slots. We follow OMP. Emptiness is represented by **count**, never by a magic card value. Display (printing "2c 7d 9s") is handled by reconstructing cards from the `cardMask` on demand — cheap, happens once per output, never in the hot loop.

## Data members

- `omp::Hand evalHand` — the accumulator. **Starts as `omp::Hand::empty()`**, not a default-constructed `omp::Hand`. (Critical — see Hard Parts.)
- `uint64_t cardMask` — bits for the board cards. For conflict checks against combos and for display reconstruction.
- count is available as `evalHand.count()`; a separate `uint8_t count_` is optional (see Hard Parts).

## Constructors / lifecycle

- `Board()` — empty board: `evalHand = omp::Hand::empty()`, `cardMask = 0`.
- `static Board fromMask(uint64_t mask)` — build a fixed board from a bitmask (used for the `--board` argument). Loops the set bits, `+=`ing each card.

In enumeration, the Calculator starts from the fixed `Board` and adds the remaining cards combinatorially. Prefer a **non-mutating** `withCard()` that returns a new `Board` (so partial boards can be reused across recursion branches), mirroring OMP's `newBoard = board + deck[i]`.

## Methods

- `void addCard(Card)` — `evalHand += omp::Hand(card.value)`; set the mask bit; (bump `count_` if kept).
- `Board withCard(Card) const` — copy, addCard, return (non-mutating variant for enumeration).
- `unsigned count() const` — `evalHand.count()`.
- `uint64_t mask() const`
- `const omp::Hand& hand() const` — for the Calculator to combine with a combo's `evalHand`.
- `bool conflicts(const Combo&) const` — `(cardMask & combo.cardMask) != 0`.
- `std::string toString() const` — reconstruct cards from `cardMask` for display.

## Invariants

- `evalHand` was initialized from `omp::Hand::empty()` and has had only valid cards added. **Never default-constructed.**
- `evalHand.count()` equals the number of cards added, equals `bitCount(cardMask)`.
- `count() <= 5`.
- A card is never added twice (its mask bit isn't already set).

## Hard parts

**`omp::Hand::empty()` seeding — critical correctness.** OMP's `Hand::empty()` is **not** a zeroed struct. It pre-sets the four suit counters to 3, so that the 4th counter bit overflows into the "flush check" bit exactly when a suit reaches 5 cards. A default-constructed `omp::Hand()` is deliberately left uninitialized for performance — using it as a starting board gives garbage rank keys, garbage flush detection, and wrong evaluations. `Board` **must** start from `omp::Hand::empty()`. If Claude Code writes `omp::Hand evalHand;` (default) instead of initializing from `empty()`, that's a silent-wrong-answer bug — check for it specifically.

**The accumulator / non-mutating pattern.** During enumeration you build many boards that share a prefix (same flop, different turn/river). Building each from scratch is wasteful. Instead, combine incrementally: `partialBoard + nextCard` is a single SSE add producing a new board, and the shared prefix is computed once. This is exactly OMP's `enumerateBoardRec` strategy (`newBoard = board + deck[i]`). Prefer `withCard()` returning a new Board over mutating in place, so recursion branches don't stomp each other.

**Redundant `count_` — optional.** `omp::Hand` already tracks count, retrievable via `evalHand.count()` (which on SSE paths is a small extract). Keeping a separate `uint8_t count_` avoids that extract at the cost of a second source of truth that must stay in sync. Recommendation: **drop `count_`, use `evalHand.count()`** for a single source of truth, unless profiling shows the extract matters (it won't for v1.0). Document whichever you choose.

**Display via mask reconstruction.** Since there's no `Card` array, `toString()` walks `cardMask`'s set bits and turns each into a `Card` for printing. This is fine because display happens once per result, not in the loop. Don't be tempted to add a `Card` array back "for convenience" — it reintroduces the sentinel problem for a feature that runs once.

## Pseudocode

```
Board():
    evalHand = omp::Hand::empty()     // NOT default-constructed
    cardMask = 0

addCard(card):
    assert !(cardMask & (1ull << card.value))   // not already present
    evalHand += omp::Hand(card.value)            // SSE add
    cardMask |= (1ull << card.value)

withCard(card) -> Board:              // non-mutating, for enumeration
    Board b = *this
    b.addCard(card)
    return b

fromMask(mask) -> Board:
    Board b
    for each set bit i in mask: b.addCard(Card(i))
    return b

toString():                           // display only
    cards = [Card(i) for each set bit i in cardMask]
    return join(cards.map(toString), " ")
```

## OMP interaction

- `board.h` includes `<omp/Hand.h>`.
- Uses `omp::Hand::empty()`, `omp::Hand(cardIdx)`, `operator+=`.
- Exposes the accumulated `omp::Hand` so the Calculator can do `combo.evalHand + board.hand()`.
- One of the three files that directly touch OMP.

## Test cases to expect

- Empty board: `count() == 0`, `mask() == 0`
- `addCard` three times → `count() == 3`, `evalHand.count() == 3`, mask has 3 bits
- `Board::fromMask` of "2c7d9s" → `toString()` reproduces those three cards
- Adding the same card twice asserts/fails
- Flush detection: a board of 5 hearts makes `evalHand.hasFlush()` true (proves `empty()` seeding works)
- `withCard` does not mutate the original board
- `conflicts`: board containing 2c conflicts with `Combo(2c, 3d)`

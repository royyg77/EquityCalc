# Card

## Purpose

`Card` represents one of the 52 playing cards. It's the atomic unit everything else is built from — `Combo` holds two, `Range` holds a list of `Combo`s. It does almost nothing on its own; its job is to be a tiny, fast, unambiguous value that converts to OMP's format for free, and to be the place where text ↔ card validation lives.

The single most important design fact: **`Card` includes no OMP header, but its byte encoding is a contract with OMP.** See Hard Parts — it's the thing most likely to silently break later.

## Why Card exists at all

OMP has no Card class because OMP is a library with no input layer — it takes bare card indices and trusts them. We're building a CLI that ingests human text ("AhKs", range files), so we have a job OMP doesn't: turning untrusted strings into validated card data, and turning results back into strings. `Card` is the boundary type (text ↔ engine). The hot loop runs on `omp::Hand`, not `Card`; `Card` has done its job by the time the loop starts. It earns its ~30 lines at the edges, not the center: validation, display, type safety (a function taking `Card` can't be handed a raw rank or player index by accident).

## Data members

One field:

- `uint8_t value` — the card encoded as `rank * 4 + suit`.

Why one byte and not two fields (`rank`, `suit`):

- Smaller. A `Combo` holds two Cards; packing into single bytes keeps these structs tight, which matters across hundreds of thousands of combos and cache lines.
- Matches OMP. OMP's card index is exactly `4 * rank + suit`, so `omp::Hand(card.value)` needs zero conversion.
- Rank/suit recover instantly: `rank = value >> 2`, `suit = value & 3`. Single instructions, no division.

## Constructors / lifecycle

- `explicit constexpr Card(uint8_t value)` — wraps a raw 0–51 index. Trusts the caller. Used internally where the value is known valid.
- `constexpr Card(uint8_t rank, uint8_t suit)` — builds from components: `value = (rank << 2) | suit`. Trusts the caller.
- `static std::optional<Card> parse(std::string_view)` — the **validating** entry point. Takes "Ah", returns a Card or `nullopt` if illegal. The only constructor fed by untrusted data, so the only one that checks.

**No sentinel / no default constructor needed.** An earlier draft considered a sentinel "no card" value (255) for undealt slots, because `Board` was going to hold `std::array<Card,5>`. That decision was reversed: `Board` now stores an `omp::Hand` accumulator + a `cardMask` + a count, with no array of Cards (see `04-board.md`). Nothing in the engine stores an optional/empty Card. Therefore `Card` does not need to be default-constructible and does not need a sentinel. Every `Card` that exists is a valid card. If Claude Code adds a default constructor or a 255 sentinel "just in case," that's unnecessary — flag it.

## Methods

- `constexpr uint8_t rank() const` → `value >> 2`
- `constexpr uint8_t suit() const` → `value & 3`
- `constexpr bool isValid() const` → `value < 52` (used to validate `parse` internals; not for sentinels)
- `std::string toString() const` → "Ah" etc.
- `static std::optional<Card> parse(std::string_view)`
- `constexpr bool operator==(Card) const` → compares `value`
- `constexpr bool operator<(Card) const` → compares `value`

The comparison operators sort rank-major automatically: rank lives in the high bits of `value`, so comparing values compares rank first, suit second. That's exactly the canonical ordering `Range` wants — free, no custom comparator.

## Invariants

- A valid `Card` has `value < 52`. There is no sentinel; every constructed Card is a real card.
- The rank/suit char mappings are fixed and **must match OMP's**. Not a place for preference.
- `Card` is trivially copyable: no heap, no destructor logic, passed **by value** everywhere. Never `const Card&` — it's one byte; a reference is bigger than the thing it points to.

## Hard parts

**The encoding contract (the dangerous one).** `Card` has no `#include` of any OMP file, so the type system shows zero coupling. But the *value* of a Card is silently assumed to equal OMP's card index everywhere we write `omp::Hand(card.value)`. If a future change "cleans up" the encoding — suit in high bits, or suits renumbered to c/d/h/s — nothing fails to compile. The evaluator silently scores the wrong cards and equities are quietly wrong. No crash, plausible-looking output: the worst kind of bug.

Mitigate two ways: (1) a `static_assert` or unit test pinning the encoding — `Card(12,1).value == 49` ("Ah"), `Card(0,0).value == 0` ("2s"); (2) a prominent comment in `card.h` stating *this encoding must match omp::Hand's card index; do not change without changing the OMP boundary.* When reviewing, confirm one of these guards exists. If `Card` is generated without pinning the encoding, correct it.

**`constexpr` everywhere.** Marking constructors and accessors `constexpr` lets the compiler fold Card operations into compile-time constants and inline the billions of `card.rank()` calls to a single shift with no call overhead. Costs nothing to add. Confirm it's there.

**Parse-don't-validate at the boundary.** The hot path constructs Cards via the trusting constructors and cannot afford per-card validation. Validation happens exactly once, in `parse()`. The two `uint8_t`/component constructors deliberately do not check their arguments — that is the design, not an oversight.

**The char maps must match OMP.** Rank: `2→0, 3→1, … 9→7, T→8, J→9, Q→10, K→11, A→12`. Suit: `s→0, h→1, c→2, d→3` (OMP order). Parsing is case-insensitive. If Claude Code invents alphabetical suit order (c/d/h/s — a natural and common mistake), the encoding contract breaks. Watch for this specifically.

## Pseudocode

Only `parse` is non-trivial.

```
parse(str):
    if str.length != 2: return nullopt
    r = charToRank(toLower(str[0]))   // 0..12 or INVALID
    s = charToSuit(toLower(str[1]))   // 0..3  or INVALID
    if r == INVALID or s == INVALID: return nullopt
    return Card((r << 2) | s)
```

`charToRank` / `charToSuit` are switch statements over the maps above. `toString` indexes two small const char arrays by rank and suit (inverse of the maps).

## OMP interaction

None at the include level. `card.h` pulls in no OMP header and names no OMP type. The coupling is purely the encoding contract: boundary classes (`Combo`, `Board`) call `omp::Hand(card.value)`, relying on `value` being a legal OMP index. `Card` itself stays pure.

## Test cases to expect

- `parse("Ah")` → rank 12, suit 1, value 49
- `parse("2s")` → rank 0, suit 0, value 0
- `parse("Ad")` → value 51 (max)
- Round-trip: for all 52 values, `parse(Card(v).toString()).value == v`
- Case-insensitivity: `parse("AH") == parse("ah") == parse("Ah")`
- Rejects garbage: `parse("Zx")`, `parse("A")`, `parse("Ahh")` → `nullopt`
- All 52 cards produce distinct values covering 0–51 exactly
- **Encoding pin:** `Card(12,1).value == 49` and `Card(0,0).value == 0`
- Ordering: `Card("2s") < Card("As")`; sorting all 52 yields rank-major order

The two review priorities: the encoding is pinned by a test or `static_assert`, and the suit mapping is s/h/c/d (not alphabetical).

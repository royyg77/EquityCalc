# Parser

## Purpose

The Parser turns range strings into `Range` objects. It's the largest single piece of original code (~400 lines) and where you diverge most from OMP — you support **weights** (`:0.5`) and **dash ranges** (`22-99`), which OMP's `CardRange` does not. It's also the most heavily tested class: every grammar rule gets its own tests.

Not a class — a set of free functions in a namespace, with one public entry point: `parseRange(std::string_view) -> Range`.

## Key decisions

1. **Error handling: STRICT.** `parseRange` throws `ParseError` on any unparseable input. `ParseError` carries a message and a 0-based cursor position into the normalized string. Do **not** copy OMP's silent-ignore behavior.
2. **Weight policy on dedup: last-write-wins.** This lives in `Range::normalize()`, not the parser — the parser only attaches weights; it does not resolve conflicts.
3. **Kicker-range semantics:** the top card is fixed, the second card (kicker) varies, both endpoints inclusive. Endpoints may be written in either order and are normalized internally.
4. **Cursor type:** `size_t` index into the cleaned string. Easier to report positions for errors than a raw `const char*`.

## Public surface

A namespace of free functions, one public entry point. No class.

```cpp
namespace equitycalc {

struct ParseError : std::runtime_error {
    size_t position;
    ParseError(std::string msg, size_t pos)
        : std::runtime_error(std::move(msg)), position(pos) {}
};

// Strict: returns a normalized Range, or throws ParseError.
Range parseRange(std::string_view input);

// Parse a single card string ("Ah", "2s", …). Throws ParseError if invalid.
Card parseCard(std::string_view s);

} // namespace equitycalc
```

Everything else is `static` (internal linkage) inside `Parser.cpp`.

## The grammar

```
range        := element ("," element)*
element      := hand (":" weight)?
weight       := float                 ; [0, ∞)

hand         := specific | pair | suited | offsuit | anytwo
              | pair_plus | suited_plus | offsuit_plus
              | pair_range | suited_range | offsuit_range
              | "random"

specific     := card card             ; "AhKs"        →  1 combo
card         := rank suit
pair         := rank rank             ; ranks equal,   "QQ"  →  6 combos
suited       := rank rank "s"         ; "AKs"          →  4 combos
offsuit      := rank rank "o"         ; "AKo"          → 12 combos
anytwo       := rank rank             ; ranks unequal, "AK"  → 16 combos

pair_plus    := pair "+"              ; "QQ+"   → QQ,KK,AA   (18 combos)
suited_plus  := suited "+"            ; "KTs+"  → KTs,KJs,KQs (12 combos)
offsuit_plus := offsuit "+"           ; same kicker-climb semantics

pair_range   := pair "-" pair         ; "22-99" → 8 pairs, either direction
suited_range := suited "-" suited     ; "A5s-A2s" → A5s,A4s,A3s,A2s (16)
offsuit_range := offsuit "-" offsuit
```

## Structure: hand-rolled recursive descent

Walk the string with a `size_t` cursor index. Each `parseX` function tries to consume a token, advances the cursor on success, and on failure backtracks the cursor to where it started. This is the same shape as OMP's `CardRange::parseHand` — read that for the state-machine pattern, then write your own with the extra features. `CardRange.cpp` is reference-only and is on the delete-after-Parser-ships list.

Internal functions (all `static`, private to `Parser.cpp`), built bottom-up:

**Step A — character-level consumers.** Cursor discipline is the contract: each either fully consumes its token and advances `cursor`, or consumes nothing and leaves `cursor` exactly where it found it. No partial consumption on a failure path.

- `parseChar(s, cursor, c) -> bool` — consume one specific char if it matches
- `parseRank(s, cursor, rank) -> bool` — one rank char → 0..12, advancing cursor on match
- `parseSuit(s, cursor, suit) -> bool` — one suit char → 0..3 (s/h/c/d), advancing cursor on match
- `parseWeight(s, cursor) -> float` — a float; throws `ParseError` if no digits consumed

`parseRank`/`parseSuit` reuse the **same** char→value maps as `Card`. Do not invent a second mapping — suits are s/h/c/d (OMP order), not alphabetical. Input is already lowercased and whitespace-stripped by `parseRange` (Step E), so these consumers can assume clean input.

**Step B — expansion helpers (pure combo generation, no parsing, no cursor).** Take ranks/flags, return `std::vector<Combo>`. Test against exact counts.

- `addCombo(out, c1, c2, w)` — push one `Combo`
- `expandCombos(out, r1, r2, suited, offsuited, w)` — expand a rank pair into combos per flags; the suited branch is skipped when `r1 == r2` (no suited pairs)
- `expandAll(out, w)` — all 1326 combos for "random"

Counts to assert: QQ → 6, AKs → 4, AKo → 12, AK → 16, random → 1326.

**Step C — `parseHand`.** Tries each grammar alternative in order, saving `size_t start = cursor` at entry and restoring on every failure path. Order the attempts so longer/more specific forms are tried before shorter ones that are prefixes of them.

Attempt order:
1. Literal `"random"` → `expandAll`
2. First rank. Try suit: if present → specific (two full rank+suit pairs, e.g. `AhKs`) → 1 combo
3. First rank, no suit. Second rank. Inspect suffix:
   - `s` → suited; then if `+` → suited-plus (kicker climb); if `-` → suited range
   - `o` → offsuit; then `+` → offsuit-plus; if `-` → offsuit range
   - no s/o and ranks equal → pair; then `+` → pair-plus; `-` → pair range
   - no s/o and ranks unequal, no suffix → anytwo

A failed `-` (e.g. `"QQ-"` with no valid second endpoint) must backtrack cleanly and report an error at the right position, not partial-consume.

**Step D — `parseElement` (hand + optional weight).**

```
parseElement(s, cursor):
    combos = parseHand(s, cursor)        // throws ParseError on failure
    weight = 1.0
    if parseChar(s, cursor, ':'):
        weight = parseWeight(s, cursor)  // throws if malformed
    for c in combos: c.weight = weight   // weight applies to ALL combos of the element
    return combos
```

**Step E — `parseRange` (public entry).**

```
parseRange(input):
    s = toLower(stripAllWhitespace(input))
    range = Range()
    cursor = 0
    loop:
        combos = parseElement(s, cursor)       // throws ParseError on failure
        for c in combos: range.add(c)
        if not parseChar(s, cursor, ','): break
    if cursor != s.size():
        throw ParseError("unexpected character at position N", cursor)
    range.normalize()                          // sort + dedup + weight policy
    return range
```

Whitespace handling: strip **all** whitespace (leading, trailing, and internal) up front, so `"  qq+ , aks "` parses identically to `"QQ+,AKs"`.

## Expansion semantics (the meat)

This is where bugs hide. Be precise:

- `"QQ"` → 6 combos (C(4,2) suit pairs for two queens)
- `"AKs"` → 4 combos (one per suit)
- `"AKo"` → 12 combos (4×4 minus the 4 same-suit)
- `"AK"` → 16 combos (suited + offsuit)
- `"random"` → all 1326 combos
- `"QQ+"` → all pairs from QQ up to AA (QQ, KK, AA) — **higher pairs**
- `"KTs+"` → kicker climbs toward the higher card: KTs, KJs, KQs (12 combos). **Not higher pairs. Kicker does not reach the top card.**
- `"22-99"` → 22, 33, 44, 55, 66, 77, 88, 99 (pair range)
- `"A5s-A2s"` → A5s, A4s, A3s, A2s (suited range, kicker varies)

## Invariants

- `parseRange` always returns a normalized `Range` (sorted, deduped) on success.
- Cursor discipline: every `parseX` either fully consumes its token and advances the cursor, or consumes nothing and leaves the cursor exactly where it found it. No partial consumption on failure.
- Parsing is case-insensitive; all whitespace is stripped before parsing begins.
- A weight applies to **all** combos produced by its element.
- `Parser.h`/`Parser.cpp` include **no OMP header** and name **no `omp::` type**.

## Hard parts

**`+` is TWO different loops (the #1 parser bug).** For pairs, `+` means "this pair and all higher pairs" (`QQ+` = QQ, KK, AA). For non-pairs, `+` means "second card climbs toward the top card" (`KTs+` = KTs, KJs, KQs — kicker climbs up to just below the top card). A single uniform `+` branch will get one of the two cases wrong. Write both and test both:

```
if r1 == r2:                           // pair-plus: higher pairs
    for r from r1 to A:
        expandCombos(out, r, r, false, true, w)
else:                                  // kicker-plus: second card climbs
    hi = max(r1, r2); lo = min(r1, r2)
    for k from lo to (hi - 1):
        expandCombos(out, hi, k, suited, offsuited, w)
```

**Dash ranges: direction-independent, both ends inclusive.** `22-99` and `99-22` parse identically; normalize endpoint order internally. For pair ranges both endpoints must be pairs; for kicker ranges the top card must match on both sides (`"A5s-K2s"` is an error). Iterate low → high after normalization; both ends are inclusive.

**Weights attach to the element, not individual combos.** `"QQ+:0.5"` — the 0.5 applies to all of QQ, KK, AA (every combo produced by the element). The weight is applied **after** expansion. Edge case: `"QQ:0.5,KK"` — QQ combos get 0.5, KK combos get the default 1.0. Duplicate-combo weight conflicts are resolved by `Range::normalize()` (last-write-wins), not here.

**Backtracking correctness.** Save `size_t start = cursor;` at entry to every `parseX`; on any failure path, `cursor = start;` before returning or throwing. The classic trap: `parseHand` reads a rank, then a suit, then tries a second rank and fails — it must reset to before the first rank so a near-miss like `"Ah"` followed by garbage reports the error at the right position.

**Reference, don't copy.** Read `CardRange.cpp` for the parsing skeleton and canonical dedup logic, then write original code. Yours adds weights, dash ranges, and strict errors — it is not a rename of OMP.

## OMP interaction

None. The Parser is pure project code. It produces `Range`s of `Combo`s (which carry `omp::Hand`s built in the Combo constructor), but the Parser itself includes no OMP header and names no OMP type. `CardRange.cpp` is reference-only and is on the delete-after-Parser-ships list.

## Pseudocode

```
expandPlus(r1, r2, suited, offsuited):
    if r1 == r2:                           // pair-plus: higher pairs
        for r from r1 to A:
            expandCombos(out, r, r, false, true, 1.0)
    else:                                  // kicker-plus: second card climbs
        hi = max(r1, r2); lo = min(r1, r2)
        for k from lo to (hi - 1):
            expandCombos(out, hi, k, suited, offsuited, 1.0)

parseRange(input):
    s = stripWhitespace(toLower(input))
    range = Range()
    cursor = 0
    loop:
        combos = parseElement(s, cursor)
        for c in combos: range.add(c)
        if not parseChar(s, cursor, ','): break
    if cursor != s.length: throw ParseError("trailing garbage", cursor)
    range.normalize()
    return range

parseElement(s, cursor):
    combos = parseHand(s, cursor)
    weight = 1.0
    if parseChar(s, cursor, ':'):
        weight = parseWeight(s, cursor)
    for c in combos: c.weight = weight
    return combos
```

## Test cases to expect

| Input | Expected |
|---|---|
| `"AhKs"` | 1 combo, exactly those two cards |
| `"QQ"` | 6 combos |
| `"AKs"` | 4 combos |
| `"AKo"` | 12 combos |
| `"AK"` | 16 combos |
| `"QQ+"` | QQ, KK, AA = 18 combos |
| `"KTs+"` | KTs, KJs, KQs = 12 combos; no higher pairs present |
| `"22-99"` | 8 pairs |
| `"99-22"` | identical to `"22-99"` |
| `"A5s-A2s"` | A5s, A4s, A3s, A2s = 16 combos |
| `"QQ+,AKs"` | union, deduped |
| `"QQ:0.5"` | 6 combos, all weight 0.5 |
| `"QQ:0.5,KK"` | QQ at 0.5, KK at 1.0 |
| `"random"` | 1326 combos |
| `"  qq+ , aks "` | identical to `"QQ+,AKs"` |
| `"AhZz"` | `ParseError` with position > 0 |
| `"QQ++"` | `ParseError` with position |
| `"QQ-"` | `ParseError` with position |
| `"5x"` | `ParseError` with position |
| `"Ah"` + trailing garbage | clean backtrack; error at the garbage position |
| `"QQ+,QQ"` | 18 combos after normalize (deduped) |

## Review checklist

- [ ] `Parser.h`/`Parser.cpp` include **no** OMP header and name **no** `omp::` type.
- [ ] Strict errors with position; not OMP's silent-ignore.
- [ ] `+` has two distinct branches (pair-up vs kicker-climb), both tested.
- [ ] Dash ranges normalize endpoint order; kicker range fixes top card, both ends inclusive.
- [ ] Every `parseX` saves cursor at entry and restores on all failure paths.
- [ ] Weight applied after expansion to every combo in the element.
- [ ] All whitespace stripped; parsing case-insensitive.
- [ ] `parseRange` ends with a trailing-garbage check (`cursor == s.size()`).
- [ ] `parseRange` calls `range.normalize()` before returning.
- [ ] Rank/suit maps are reused from `Card`, not redefined; suits are s/h/c/d.
- [ ] Original code, not a renamed `CardRange`.

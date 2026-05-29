# Parser

## Purpose

The Parser turns range strings into `Range` objects. It's the largest single piece of original code (~400 lines) and where you diverge most from OMP — you support **weights** (`:0.5`) and **dash ranges** (`22-99`), which OMP's `CardRange` does not. It's also the most heavily tested class: every grammar rule gets its own tests.

Not a class — a set of free functions in a namespace, with one public entry point: `parseRange(std::string_view) -> Range` (or a `Result<Range, ParseError>` if you want structured errors).

## The grammar (from the outline)

```
range        := element ("," element)*
element      := hand (":" weight)?
weight       := float                 ; typically [0, 1]

hand         := specific | pair | suited | offsuit | anytwo
              | pair_plus | suited_plus | offsuit_plus
              | pair_range | suited_range | offsuit_range
              | "random"

specific     := card card             ; "AhKs"
card         := rank suit
pair         := rank rank             ; ranks equal, "AA"
suited       := rank rank "s"         ; "AKs"
offsuit      := rank rank "o"         ; "AKo"
anytwo       := rank rank             ; ranks unequal, "AK" (both suited+offsuit)

pair_plus    := pair "+"              ; "QQ+"
suited_plus  := suited "+"            ; "AKs+"  (wrong — see Hard Parts; really kicker climb)
offsuit_plus := offsuit "+"           ; "AKo+"

pair_range    := pair "-" pair         ; "22-99"
suited_range  := suited "-" suited      ; "A5s-A2s"
offsuit_range := offsuit "-" offsuit
```

## Structure: hand-rolled recursive descent

Walk the string with a cursor (a `const char*` or index). Each `parseX` function tries to consume a token, advances the cursor on success, and on failure backtracks the cursor to where it started. This is the same shape as OMP's `CardRange::parseHand` — read that for the state-machine pattern, then write your own with the extra features.

Internal functions (private to the .cpp):

- `parseElement(s, cursor) -> combos` — one hand + optional weight
- `parseHand(s, cursor) -> combos`
- `parseRank(s, cursor) -> rank`, `parseSuit(s, cursor) -> suit`
- `parseWeight(s, cursor) -> float`
- `parseChar(s, cursor, c) -> bool` — try to consume a specific char
- expansion helpers: `expandPair`, `expandSuited`, `expandOffsuit`, `expandAnytwo`, `expandPlus`, `expandRange`

## Expansion semantics (the meat)

This is where bugs hide. Be precise:

- `"QQ"` → 6 combos (C(4,2) suit pairs for two queens)
- `"AKs"` → 4 combos (one per suit)
- `"AKo"` → 12 combos (4×4 minus the 4 same-suit)
- `"AK"` → 16 combos (suited + offsuit)
- `"random"` → all 1326 combos
- `"QQ+"` → all pairs from QQ up to AA (QQ, KK, AA) — **higher pairs**
- `"AKs+"` → kicker climb: this means A-with-kicker-K-and-up... but K is already the top kicker below A, so `"KTs+"` → KTs, KJs, KQs. **The kicker climbs toward the higher card, not the pair.**
- `"22-99"` → 22, 33, 44, 55, 66, 77, 88, 99 (pair range)
- `"A5s-A2s"` → A5s, A4s, A3s, A2s (suited range, kicker descends)

## Invariants

- `parseRange` always returns a normalized `Range` (sorted, deduped) on success.
- Cursor discipline: every `parseX` either fully consumes its token and advances the cursor, or consumes nothing and leaves the cursor where it found it (clean backtrack). No partial consumption on failure.
- Parsing is case-insensitive and ignores surrounding whitespace.
- A weight applies to **all** combos produced by its element.

## Hard parts

**`+` semantics differ for pairs vs non-pairs.** For pairs, `+` means "this pair and all higher pairs" (`QQ+` = QQ, KK, AA). For non-pairs, `+` means "this hand and all higher *kickers* with the same top card" (`KTs+` = KTs, KJs, KQs — the second card climbs up to just below the top card). These are two different loops. OMP's `addCombosPlus` implements both branches; read it. If Claude Code treats `+` uniformly, it'll get one of the two cases wrong. This is the most common parser bug.

**Dash ranges and direction.** `22-99` ascends pairs. `A5s-A2s` descends kickers. The user can write the endpoints in either order (`22-99` or `99-22`); normalize internally. Decide and document the exact semantics for kicker ranges (which card is fixed, which varies, inclusive both ends).

**Weights attach to the element, not individual combos in the string.** `"QQ+:0.5"` — does the 0.5 apply to QQ, or to QQ+KK+AA? It applies to the whole element (all of QQ+). Make sure the weight is applied after expansion, to every produced combo. Edge case: `"QQ:0.5,KK"` — QQ combos get 0.5, KK combos get the default 1.0.

**Error handling — strict vs lenient.** OMP silently ignores anything it can't parse (it just stops). For a CLI you probably want to *tell the user* their input was malformed. Decide:
- **Strict:** any unparseable token → return an error with a position and message. Better UX.
- **Lenient (OMP-style):** parse what you can, ignore the rest. Simpler, but silently drops typos.
Recommendation: strict for v1.0, with a clear message ("unexpected character 'x' at position 4"). Document the choice. If Claude Code copies OMP's silent-ignore behavior, that's a UX regression worth flagging.

**Backtracking correctness.** The classic recursive-descent trap: a `parseX` that consumes some characters then fails must restore the cursor. E.g. `parseHand` reads a rank, then a suit, then tries a second rank and fails — it must reset the cursor to before the first rank so the caller can try a different rule. OMP does this with a `backtrack` pointer saved at entry. Verify the generated code saves and restores the cursor on every failure path.

**Reference, don't copy.** Read `CardRange.cpp` for the parsing skeleton and the canonical dedup logic, then write your own. Yours has weights, dash ranges, and (recommended) real error messages — it is not a rename of theirs.

## OMP interaction

None. The Parser is pure your-code. It produces `Range`s of `Combo`s (which carry `omp::Hand`s built in the Combo constructor), but the Parser itself names no OMP type and calls no OMP function. `CardRange.cpp` is read as a reference only and is on the delete-after-Parser-ships list.

## Pseudocode (selective — not every rule)

```
parseRange(input):
    s = stripWhitespace(toLower(input))
    range = Range()
    cursor = 0
    if s == "random": return Range of all 1326 combos
    loop:
        combos = parseElement(s, cursor)        // advances cursor
        if combos is error: return error
        for c in combos: range.add(c)
        if not parseChar(s, cursor, ','): break
    if cursor != s.length: return error("trailing garbage")
    range.normalize()
    return range

parseElement(s, cursor):
    combos = parseHand(s, cursor)
    if combos is error: return error
    weight = 1.0
    if parseChar(s, cursor, ':'):
        weight = parseWeight(s, cursor)
    for c in combos: c.weight = weight
    return combos

expandPlus(r1, r2, suited, offsuit):     // after detecting '+'
    if r1 == r2:                          // pair+ : higher pairs
        for r from r1 to A: addPair(r, ...)
    else:                                 // kicker+ : second card climbs
        hi = max(r1, r2); lo = min(r1, r2)
        for k from lo to (hi - 1): addCombos(hi, k, suited, offsuit)
```

## Test cases to expect (one family per grammar rule)

- Specific: `"AhKs"` → 1 combo with exactly those cards
- Pair: `"QQ"` → 6 combos
- Suited / offsuit / anytwo: `"AKs"` → 4, `"AKo"` → 12, `"AK"` → 16
- Pair plus: `"QQ+"` → QQ, KK, AA (18 combos)
- Kicker plus: `"KTs+"` → KTs, KJs, KQs (12 combos); verify it does NOT include higher pairs
- Pair range: `"22-99"` → 8 pairs; `"99-22"` parses identically
- Kicker range: `"A5s-A2s"` → A5s,A4s,A3s,A2s (16 combos)
- Combination: `"QQ+,AKs"` → union, deduped
- Weights: `"QQ:0.5"` → all 6 combos weight 0.5; `"QQ:0.5,KK"` → QQ at 0.5, KK at 1.0
- `"random"` → 1326 combos
- Case/whitespace: `"  qq+ , aks "` parses same as `"QQ+,AKs"`
- Errors (if strict): `"AhZz"`, `"QQ++"`, `"QQ-"`, `"5x"` → error with position
- Backtrack: a near-miss like `"Ah"` followed by non-card chars resets cleanly

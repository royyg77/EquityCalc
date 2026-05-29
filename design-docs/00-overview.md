# EquityCalc v1.0 — Engine Design Docs

These documents describe the seven classes (plus the Parser free functions) that make up the v1.0 engine. They exist so you can supervise Claude Code: read the doc for a class, then check that the generated code matches the intent, invariants, and OMP boundary described here. If the code drifts from these docs, that's a signal to correct it before it compounds.

Read order matches the build order; each class builds on the previous:

1. `01-card.md`
2. `02-combo.md`
3. `03-range.md`
4. `04-board.md`
5. `05-parser.md`
6. `06-evaluator.md`
7. `07-calculator.md`
8. `08-result.md`

---

## What v1.0 is

A command-line program. Input: 2–6 hand-range strings (or files), an optional board, optional dead cards. Output: equity / win / tie per player, printed as text (or JSON). No GUI, no web server, no Python — those are v2.0.

Example:

```
$ equitycalc "AhKs" "QdQc"
P1 (AhKs):  46.20%   wins: 791,032   ties: 0
P2 (QdQc):  53.80%   wins: 921,272   ties: 0
1,712,304 scenarios evaluated in 0.34s
```

---

## Shared conventions (apply to every class)

### The card encoding contract

Every card is a single byte: `value = rank * 4 + suit`, equivalently `value = (rank << 2) | suit`.

- `rank`: 0–12. 0 = deuce … 8 = ten, 9 = jack, 10 = queen, 11 = king, 12 = ace.
- `suit`: 0–3, in **OMP's order**: `s = 0, h = 1, c = 2, d = 3`.
- `value`: 0–51.

**This encoding is a contract with OMPEval.** OMP's `omp::Hand(cardIdx)` expects exactly this layout (`4 * rank + suit`, suits s/h/c/d). Anywhere we write `omp::Hand(card.value)`, we rely on it. Changing the encoding — reordering suits, moving suit to the high bits — does not produce a compile error. It produces silently wrong equities. This is the single most dangerous class of bug in the project. Every doc that touches the encoding repeats this warning, and the Card doc specifies a pinning test.

Note: alphabetical suit display (c/d/h/s) is a **presentation** choice only. If we want output sorted alphabetically, that's a comparator in the output layer. The internal `value` stays OMP-native (s/h/c/d). Never let a display preference change the encoding.

### Naming and style

- C++20. Headers in `include/equitycalc/`, sources in `src/`.
- `snake_case` files, `PascalCase` types, `camelCase` methods, trailing-underscore private members (`combos_`).
- Validate untrusted input once at the boundary (the parser). Internal code trusts that values are valid; it does not re-check. Cards, combos, and boards constructed inside the engine are assumed valid.

### Mode selection: scenario count, not player count

The Calculator chooses enumeration vs Monte Carlo by the **estimated total number of scenarios**, not by player count.

```
totalScenarios = preflopCombos * postflopBoards
  preflopCombos  = product of each range's combo count (pre-conflict-removal estimate)
  postflopBoards = C(cardsRemainingInDeck, boardCardsStillToCome)

if totalScenarios < THRESHOLD: enumerate (exact)
else:                          monte carlo (sampled)
```

`THRESHOLD` lands somewhere around 1e7–1e8; tune once benchmarkable. Player count falls out naturally — 6 players almost always pushes the product past the threshold and auto-selects MC, while a 6-player all-specific-hands river spot (tiny state space) correctly enumerates. Player count alone would get that second case wrong.

### Dead cards

Dead cards, board cards, and each combo's cards all live in the same 52-bit `uint64_t` mask format (bit N set = card N in use). "Is this card unavailable?" is a single `&`. Dead cards are pre-loaded into the "used cards" mask before the loop begins, alongside board cards. A dead card is just "an unavailable card that belongs to no one" — the conflict check doesn't care why a card is unavailable.

### Player count

v1.0 supports 2–6 players (`MAX_PLAYERS = 6`). Early build steps use 2 players because it's the simplest thing to test first; the finished engine is N-player. The `winsByPlayerMask` tally (see Calculator) handles any player count up to 6.

### The OMP boundary

Only three of your files include an OMP header:

- `combo.h` — carries an `omp::Hand` (the cached, pre-built hand)
- `board.h` — carries an `omp::Hand` accumulator
- `evaluator.h` — holds an `omp::HandEvaluator`

Every other class is OMP-free at the include level. `card.h`, `range.h`, `parser.h`, `result.h` name no OMP type. `calculator.cpp` touches `omp::Hand` only transitively through Combo and Board, and scores only through Evaluator.

### Dependency direction (no cycles)

```
CLI ─► Parser ─► Range ─► Combo ─► Card
 │                         └─► omp::Hand
 └─► Calculator ─► Range
        ├─► Board ─► omp::Hand
        ├─► Evaluator ─► omp::HandEvaluator
        └─► Result
```

Everything points down/right. Nothing points back. This is what lets you build and test bottom-up: Card alone, then Combo, then Range, and so on.

---

## How to use these docs to supervise Claude Code

For each class, the doc lists **Invariants** and **Hard parts**. When reviewing generated code, those are the two sections to check against:

1. Do the invariants hold? (e.g. Combo always stores higher card first; Board always starts from `omp::Hand::empty()`, never a default `omp::Hand`.)
2. Did it handle the hard parts, or take the naive shortcut? (e.g. did Combo cache the `omp::Hand`, or does it rebuild it every evaluation?)

If a generated class violates an invariant or fumbles a hard part, that's drift. Correct it immediately — these compound.

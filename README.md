# EquityCalc

A poker hand-range equity calculator in modern C++ (C++20).

Given 2–6 hand-range strings, an optional board, and optional dead cards,
EquityCalc computes each player's equity, win rate, and tie rate — either by
**exact enumeration** (every matchup × every runout) or by **Monte Carlo**
sampling when the exact space is too large. Mode is selected automatically from
the estimated scenario count, not the player count.

> **v1.0 is a correct, single-threaded engine whose internal shape is already
> parallel-ready.** Multithreading and the micro-optimizations (libdivide,
> suit isomorphism, alias-table sampling, a 2-player lookup table) are deferred
> to v1.x; the seams for each are marked in the source. See
> [`docs/07-calculator.md`](docs/07-calculator.md).

---

## Status

| Component  | State        |
| ---------- | ------------ |
| Card       | complete     |
| Combo      | complete     |
| Range      | complete     |
| Board      | complete     |
| Parser     | complete     |
| Evaluator  | complete     |
| Calculator | complete     |
| Result     | complete     |

v1.0 is a command-line program: no GUI, no web server, no Python. Those are v2.0.

---

## Build

Requirements: a C++20 compiler (GCC 11+ / Clang 14+ / MSVC 19.3+) and CMake 3.20+.
OMPEval is vendored at `engine/third_party/OMPEval/` — no external package needed.

```bash
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The CLI binary lands at `build/cli/equitycalc`. Tests build alongside it.

---

## Usage

```text
equitycalc <range1> <range2> [range3 ... range6]
           [--board <cards>] [--dead <cards>] [--json]
```

- **Ranges** are hand-range strings: specific hands (`AhKs`), pairs and plus
  notation (`QQ+`), suited/offsuit classes (`AKs`, `AKo`), dash ranges
  (`98s-65s`), and per-combo weights (`AKs:0.5`). Comma-separate within one
  player's range: `"QQ+,AKs:0.5,AhKh"`.
- **`--board`** fixes community cards, e.g. `--board AhKd7c`.
- **`--dead`** removes cards from the deck without assigning them to a player.
- **`--json`** emits machine-readable output instead of the text table.

### Example — preflop, exact

```text
$ equitycalc "AhKs" "QdQc"
P1: equity:  46.2030%   win:  46.2030% (791032)   tie:   0.0000% (0.0)
P2: equity:  53.7970%   win:  53.7970% (921272)   tie:   0.0000% (0.0)
1712304 scenarios in 0.34s (enumeration)
```

### Example — range vs range with a board

```text
$ equitycalc "QQ+,AKs" "JJ-99,AQs+" --board Ah7d2c
P1: equity:  61.4xxx%   win:  ...
P2: equity:  38.5xxx%   win:  ...
... scenarios in ...s (enumeration)
```

### Example — large space auto-selects Monte Carlo

```text
$ equitycalc "22+,A2s+,K2s+,Q2s+,J2s+" "22+,A2s+,K2s+" "random-equivalent-wide-range"
P1: equity:  ...%   ...
... scenarios in ...s (monte carlo)  (±0.05%)
```

The trailing `(±x%)` is the 95% confidence-interval half-width, shown only in
Monte Carlo mode. Exact runs report no interval.

### JSON output

`--json` emits raw fractions and weighted counts (not formatted percentages);
consumers compute their own display, e.g. `winPct = wins[i] / totalWeightTallied
* 100`.

```json
{
  "players": 2,
  "mode": "enumeration",
  "totalScenarios": 1712304,
  "totalWeightTallied": 1712304.000000,
  "timeSeconds": 0.3400,
  "ciHalfWidth": 0.000000,
  "equity": [0.462030, 0.537970],
  "wins":   [791032.000000, 921272.000000],
  "ties":   [0.000000, 0.000000]
}
```

---

## How it works (one paragraph)

The Parser turns each range string into a `Range` of weighted `Combo`s, each
caching a pre-built `omp::Hand`. The Calculator treats the preflop space as a
flat matchup index, decodes each index to one combo per player (skipping
inter-player card conflicts), enumerates or samples board runouts, and scores
every fully-dealt scenario through a single shared `Evaluator`. All scoring
writes into one accumulator keyed by *winner bitmask*, so multiway ties and the
weighted denominator are tracked exactly. **Equity is normalized by the summed
weight of scenarios actually scored — never by a scenario count or a precomputed
product** — which keeps weighted ranges correct. The full design lives in
[`docs/`](docs/) (`00-overview.md` first).

---

## Project layout

```text
engine/
├── include/equitycalc/   # public headers (card, combo, range, board,
│                         #   parser, evaluator, calculator, result)
├── src/                  # implementations
├── cli/                  # command-line front end
├── tests/                # per-class unit tests
├── bench/                # benchmark harness + standardized spots (v1.x baseline)
└── third_party/OMPEval/  # vendored evaluator (ISC license)
```

bindings/, backend/, frontend/, deploy/ are v2.0 scaffolding.

---

## Testing

```bash
ctest --test-dir build --output-on-failure
```

Coverage includes the canonical `AA` vs `KK` ≈ 81.9% / 18.1% regression, a
weighted-normalization guard (fails if the denominator is ever a count or a
precomputed product), multiway tie handling, exact-vs-MC mode parity, and
mode-selection thresholds. See `docs/07-calculator.md` → "Test cases to expect".

---

## Roadmap

- **v1.0** — single-threaded exact + Monte Carlo engine, CLI. *(current)*
- **v1.1** — multithreaded enumeration and sampling (the accumulator `merge`
  seam); richer multiway breakdowns.
- **v1.2** — suit isomorphism, libdivide matchup decode, alias-table sampler,
  2-player lookup table.
- **v2.0** — pybind11 bindings, FastAPI backend, React frontend.

---

## License

OMPEval is included under its ISC license (`engine/third_party/OMPEval/LICENSE`).

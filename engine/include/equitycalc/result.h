#pragma once

#include <cstdint>
#include <string>

namespace equitycalc {

static constexpr unsigned MAX_PLAYERS = 6;

enum class Mode { Enumeration, MonteCarlo };

// Output of one calculation. Passive data carrier — filled by Calculator,
// read by the CLI. No logic beyond formatting.
struct Result {
    unsigned players{0};

    // Per-player equity in [0, 1]. Sums to ~1.0 across all players.
    double equity[MAX_PLAYERS]{};

    // Outright wins per player (weighted). Use double to support fractional weights.
    double wins[MAX_PLAYERS]{};

    // Tie-share equity per player (e.g. 0.5 per player for a two-way chop).
    double ties[MAX_PLAYERS]{};

    // Full joint outcome breakdown indexed by winner bitmask.
    // tally[0b011] = weighted count of scenarios where P0 and P1 chopped.
    // Preserved for multiway analysis and v1.1 breakdowns.
    double winsByPlayerMask[1 << MAX_PLAYERS]{};

    uint64_t totalScenarios{0};   // scenarios evaluated (or effective weighted count)
    double   totalWeightTallied{0.0}; // denominator used for equity normalization
    double   timeSeconds{0.0};    // wall-clock duration
    Mode     mode{Mode::Enumeration};
    double   ciHalfWidth{0.0};    // MC confidence-interval half-width; 0 for enumeration

    // Human-readable columnar output. Shows ±ciHalfWidth only in MC mode.
    std::string format() const;

    // Machine-readable JSON, for --json flag.
    std::string toJson() const;
};

}  // namespace equitycalc

#pragma once

#include "equitycalc/Combo.h"
#include <cstdint>
#include <vector>

namespace equitycalc {

// One player's set of possible hole-card combos with weights.
//
// After normalize(), combos_ is sorted (canonical order) and deduplicated.
// Weight-conflict policy: last-write-wins (later add() overrides earlier weight).
// Equity is normalized by totalWeight(), not by size() — see Calculator.
class Range {
public:
    Range() = default;
    // explicit Range(std::vector<Combo> combos);
    // Fix: change vector<Combo> to ComboVec
    explicit Range(ComboVec combos);

    // Append a combo; dedup/sort deferred to normalize().
    void add(Combo c);

    // Sort canonical, dedup with last-write-wins weight policy.
    void normalize();

    // Drop any combo whose cardMask overlaps deadMask (board + dead cards).
    // Call once after board/dead cards are known; totalWeight() stays consistent.
    void removeConflicts(uint64_t deadMask);

    const ComboVec& combos() const noexcept { return combos_; }
    size_t size() const noexcept { return combos_.size(); }

    // Sum of combo weights — use this for equity normalization, not size().
    double totalWeight() const;

private:
    ComboVec combos_;
};

}  // namespace equitycalc

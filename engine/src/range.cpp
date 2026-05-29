#include "equitycalc/range.h"
#include <algorithm>
#include <numeric>

namespace equitycalc {

Range::Range(std::vector<Combo> combos) : combos_(std::move(combos)) {}

void Range::add(Combo c) {
    combos_.push_back(c);
}

void Range::normalize() {
    // Sort canonical (Combo::operator< compares c1 then c2 by card value).
    std::sort(combos_.begin(), combos_.end());

    // Dedup with last-write-wins weight policy:
    // When two adjacent combos have the same cards, keep the later one's weight.
    // std::unique keeps the first, so we walk manually.
    std::vector<Combo> deduped;
    deduped.reserve(combos_.size());
    for (auto& c : combos_) {
        if (!deduped.empty() && deduped.back() == c) {
            deduped.back().weight = c.weight; // last-write-wins
        } else {
            deduped.push_back(c);
        }
    }
    combos_ = std::move(deduped);
}

void Range::removeConflicts(uint64_t deadMask) {
    combos_.erase(
        std::remove_if(combos_.begin(), combos_.end(),
                       [deadMask](const Combo& c) {
                           return (c.cardMask & deadMask) != 0;
                       }),
        combos_.end());
}

double Range::totalWeight() const {
    double total = 0.0;
    for (const auto& c : combos_) total += static_cast<double>(c.weight);
    return total;
}

} // namespace equitycalc

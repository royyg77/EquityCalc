#include "equitycalc/Range.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using equitycalc::Card;
using equitycalc::Combo;
using equitycalc::Range;

static Card C(const char* s) { return *Card::parse(s); }

// Build all 6 QQ combos with an optional weight.
static std::vector<Combo> allQQ(float w = 1.0f) {
    return {
        Combo(C("Qd"), C("Qc"), w),
        Combo(C("Qd"), C("Qh"), w),
        Combo(C("Qd"), C("Qs"), w),
        Combo(C("Qc"), C("Qh"), w),
        Combo(C("Qc"), C("Qs"), w),
        Combo(C("Qh"), C("Qs"), w),
    };
}

// ── Empty range ───────────────────────────────────────────────────────────────

TEST(RangeEmpty, DefaultConstructed) {
    Range r;
    EXPECT_EQ(r.size(), 0u);
    EXPECT_DOUBLE_EQ(r.totalWeight(), 0.0);
    EXPECT_TRUE(r.combos().empty());
}

// ── size / combos accessor ────────────────────────────────────────────────────

TEST(RangeAdd, AddAndSize) {
    Range r;
    r.add(Combo(C("Ah"), C("Ks")));
    r.add(Combo(C("Qd"), C("Jc")));
    r.normalize();
    EXPECT_EQ(r.size(), 2u);
}

TEST(RangeAdd, CombosAccessorMatchesSize) {
    Range r;
    r.add(Combo(C("Ah"), C("Ks")));
    r.normalize();
    EXPECT_EQ(r.combos().size(), r.size());
}

// ── normalize: 6 QQ combos ────────────────────────────────────────────────────

TEST(RangeNormalize, SixQQCombos) {
    Range r;
    for (auto& c : allQQ()) r.add(c);
    r.normalize();
    EXPECT_EQ(r.size(), 6u) << "QQ expands to exactly 6 combos";
}

// ── normalize: dedup ──────────────────────────────────────────────────────────

TEST(RangeNormalize, DuplicateComboDeduped) {
    Range r;
    r.add(Combo(C("Ah"), C("Ks")));
    r.add(Combo(C("Ah"), C("Ks")));
    r.normalize();
    EXPECT_EQ(r.size(), 1u) << "identical combo added twice should dedup to one";
}

TEST(RangeNormalize, DuplicateComboReversedInputDeduped) {
    Range r;
    r.add(Combo(C("Ah"), C("Ks")));
    r.add(Combo(C("Ks"), C("Ah"))); // reversed cards, same canonical combo
    r.normalize();
    EXPECT_EQ(r.size(), 1u) << "reversed-input duplicate should still dedup";
}

// ── normalize: last-write-wins weight policy ──────────────────────────────────

TEST(RangeNormalize, LastWriteWinsWeight) {
    // Add all QQ combos at weight 0.5, then override QhQd with weight 1.0.
    Range r;
    for (auto& c : allQQ(0.5f)) r.add(c);
    r.add(Combo(C("Qh"), C("Qd"), 1.0f)); // QhQd == QdQh after canonicalization
    r.normalize();
    EXPECT_EQ(r.size(), 6u) << "still 6 unique QQ combos";

    // Find QdQh in the normalized list and check its weight.
    Combo target(C("Qd"), C("Qh"));
    bool found = false;
    for (const auto& c : r.combos()) {
        if (c == target) {
            EXPECT_FLOAT_EQ(c.weight, 1.0f) << "QdQh should have weight 1.0 (last-write-wins)";
            found = true;
        } else {
            EXPECT_FLOAT_EQ(c.weight, 0.5f) << "other QQ combos should keep weight 0.5";
        }
    }
    EXPECT_TRUE(found) << "QdQh combo must be present after normalize";
}

// ── normalize: canonical sorted order ─────────────────────────────────────────

TEST(RangeNormalize, SortedCanonicalOrder) {
    // Add QQ combos in non-sorted order; after normalize they should be sorted.
    Range r;
    r.add(Combo(C("Qh"), C("Qs"))); // smallest QQ combo (c1=Qh=41, c2=Qs=40)
    r.add(Combo(C("Qd"), C("Qc"))); // largest QQ combo  (c1=Qd=43, c2=Qc=42)
    r.add(Combo(C("Qc"), C("Qs")));
    r.normalize();

    const auto& v = r.combos();
    for (size_t i = 1; i < v.size(); ++i) {
        EXPECT_LT(v[i - 1], v[i]) << "combos not in strict ascending order at index " << i;
    }
}

TEST(RangeNormalize, NormalizeIdempotent) {
    Range r;
    for (auto& c : allQQ()) r.add(c);
    r.normalize();
    auto first = r.combos();
    r.normalize();
    auto second = r.combos();
    EXPECT_EQ(first.size(), second.size());
    for (size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i], second[i]);
        EXPECT_FLOAT_EQ(first[i].weight, second[i].weight);
    }
}

// ── removeConflicts ───────────────────────────────────────────────────────────

TEST(RangeRemoveConflicts, QhBoardRemovesThreeQQCombos) {
    Range r;
    for (auto& c : allQQ()) r.add(c);
    r.normalize();
    ASSERT_EQ(r.size(), 6u);

    // Board contains Qh — 3 combos include Qh (QdQh, QcQh, QhQs) and get removed.
    uint64_t deadMask = uint64_t(1) << C("Qh").value;
    r.removeConflicts(deadMask);
    EXPECT_EQ(r.size(), 3u) << "3 QQ combos containing Qh should be removed";

    // Verify none of the remaining combos include Qh.
    for (const auto& c : r.combos()) {
        EXPECT_FALSE(c.conflicts(deadMask))
            << "remaining combo " << c.c1.toString() << c.c2.toString() << " still contains Qh";
    }
}

TEST(RangeRemoveConflicts, EmptyMaskRemovesNothing) {
    Range r;
    r.add(Combo(C("Ah"), C("Ks")));
    r.normalize();
    r.removeConflicts(0u);
    EXPECT_EQ(r.size(), 1u);
}

TEST(RangeRemoveConflicts, FullOverlapRemovesAll) {
    Range r;
    r.add(Combo(C("Ah"), C("Ks")));
    r.normalize();
    uint64_t deadMask = (uint64_t(1) << C("Ah").value) | (uint64_t(1) << C("Ks").value);
    r.removeConflicts(deadMask);
    EXPECT_EQ(r.size(), 0u);
}

// ── totalWeight ───────────────────────────────────────────────────────────────

TEST(RangeTotalWeight, EqualWeights) {
    Range r;
    for (auto& c : allQQ(1.0f)) r.add(c);
    r.normalize();
    EXPECT_DOUBLE_EQ(r.totalWeight(), 6.0);
}

TEST(RangeTotalWeight, AAOneKKHalf) {
    // AA:1.0 (6 combos) + KK:0.5 (6 combos) = 6.0 + 3.0 = 9.0
    Range r;
    std::vector<std::pair<const char*, const char*>> aa = {
        {"Ad", "Ac"}, {"Ad", "Ah"}, {"Ad", "As"},
        {"Ac", "Ah"}, {"Ac", "As"}, {"Ah", "As"},
    };
    for (auto [a, b] : aa) r.add(Combo(C(a), C(b), 1.0f));

    std::vector<std::pair<const char*, const char*>> kk = {
        {"Kd", "Kc"}, {"Kd", "Kh"}, {"Kd", "Ks"},
        {"Kc", "Kh"}, {"Kc", "Ks"}, {"Kh", "Ks"},
    };
    for (auto [a, b] : kk) r.add(Combo(C(a), C(b), 0.5f));

    r.normalize();
    EXPECT_DOUBLE_EQ(r.totalWeight(), 9.0);
}

TEST(RangeTotalWeight, UpdatedAfterRemoveConflicts) {
    Range r;
    for (auto& c : allQQ(1.0f)) r.add(c);
    r.normalize();
    ASSERT_DOUBLE_EQ(r.totalWeight(), 6.0);

    uint64_t deadMask = uint64_t(1) << C("Qh").value;
    r.removeConflicts(deadMask);
    // 3 combos removed; 3 remain each at weight 1.0
    EXPECT_DOUBLE_EQ(r.totalWeight(), 3.0);
}

// ── vector constructor ────────────────────────────────────────────────────────

TEST(RangeVectorCtor, ConstructFromVector) {
    auto combos = allQQ();
    Range r(std::move(combos));
    // vector ctor does not normalize; just confirms the combos were stored
    EXPECT_EQ(r.size(), 6u);
}

#include "equitycalc/Combo.h"
#include <gtest/gtest.h>
#include <cstdint>

#include <bit>


using equitycalc::Card;
using equitycalc::Combo;

// Parse a card string or abort — keeps test bodies readable.
static Card C(const char* s) { return *Card::parse(s); }

// ── Canonical ordering ────────────────────────────────────────────────────────

TEST(ComboCanonicalOrder, AlreadyOrdered) {
    // Ah(49) > Ks(44) — input order matches canonical order
    Combo combo(C("Ah"), C("Ks"));
    EXPECT_EQ(combo.c1, C("Ah")) << "c1 should be Ah (higher value=49)";
    EXPECT_EQ(combo.c2, C("Ks")) << "c2 should be Ks (lower value=44)";
}

TEST(ComboCanonicalOrder, ReversedInputCanonicalized) {
    // Give Ks first, Ah second — constructor must swap to c1=Ah, c2=Ks
    Combo combo(C("Ks"), C("Ah"));
    EXPECT_EQ(combo.c1, C("Ah")) << "c1 should be Ah after canonicalization";
    EXPECT_EQ(combo.c2, C("Ks")) << "c2 should be Ks after canonicalization";
}

TEST(ComboCanonicalOrder, SameResultBothDirections) {
    Combo forward(C("Ah"), C("Ks"));
    Combo reverse(C("Ks"), C("Ah"));
    EXPECT_EQ(forward.c1, reverse.c1);
    EXPECT_EQ(forward.c2, reverse.c2);
}

// ── cardMask ──────────────────────────────────────────────────────────────────

TEST(ComboCardMask, ExactlyTwoBitsSet) {
    Combo combo(C("Ah"), C("Ks"));
    // int bits = __builtin_popcountll(combo.cardMask);
    // int bits = (int)omp::bitCount(combo.cardMask);
    int bits = std::popcount(combo.cardMask);

    EXPECT_EQ(bits, 2)
        << "cardMask=" << combo.cardMask << " should have exactly 2 bits set";
}

TEST(ComboCardMask, CorrectBitsSet) {
    Card ah = C("Ah"), ks = C("Ks");
    Combo combo(ah, ks);
    EXPECT_TRUE(combo.cardMask & (uint64_t(1) << ah.value))
        << "bit for Ah (value=" << (int)ah.value << ") should be set";
    EXPECT_TRUE(combo.cardMask & (uint64_t(1) << ks.value))
        << "bit for Ks (value=" << (int)ks.value << ") should be set";
}

// ── weight ────────────────────────────────────────────────────────────────────

TEST(ComboWeight, DefaultIsOne) {
    Combo combo(C("Ah"), C("Ks"));
    EXPECT_FLOAT_EQ(combo.weight, 1.0f) << "default weight should be 1.0";
}

TEST(ComboWeight, CustomWeight) {
    Combo combo(C("Ah"), C("Ks"), 0.5f);
    EXPECT_FLOAT_EQ(combo.weight, 0.5f)
        << "Combo(Ah,Ks,0.5).weight should be 0.5";
}

// ── evalHand ──────────────────────────────────────────────────────────────────

TEST(ComboEvalHand, CountIsTwo) {
    // The cached omp::Hand must represent exactly 2 cards.
    Combo combo(C("Ah"), C("Ks"));
    EXPECT_EQ(combo.evalHand.count(), 2u)
        << "evalHand.count() should be 2 — one entry per hole card";
}

// ── conflicts(Combo) ──────────────────────────────────────────────────────────

TEST(ComboConflicts, SharedCardIsConflict) {
    // Combo(Ah,Ks) and Combo(Ah,Qd) share Ah → conflict
    Combo c1(C("Ah"), C("Ks"));
    Combo c2(C("Ah"), C("Qd"));
    EXPECT_TRUE(c1.conflicts(c2))
        << "Combo(Ah,Ks) should conflict with Combo(Ah,Qd) — both contain Ah";
}

TEST(ComboConflicts, NoSharedCard) {
    // Combo(Ah,Ks) and Combo(2c,3d) share no card → no conflict
    Combo c1(C("Ah"), C("Ks"));
    Combo c2(C("2c"), C("3d"));
    EXPECT_FALSE(c1.conflicts(c2))
        << "Combo(Ah,Ks) should not conflict with Combo(2c,3d)";
}

TEST(ComboConflicts, SymmetricProperty) {
    Combo c1(C("Ah"), C("Ks"));
    Combo c2(C("Ah"), C("Qd"));
    EXPECT_EQ(c1.conflicts(c2), c2.conflicts(c1))
        << "conflicts() should be symmetric";
}

// ── conflicts(uint64_t mask) ──────────────────────────────────────────────────

TEST(ComboConflictsMask, ConflictsWithBoardContainingSharedCard) {
    Combo combo(C("Ah"), C("Ks"));
    uint64_t boardMask = uint64_t(1) << C("Ah").value;
    EXPECT_TRUE(combo.conflicts(boardMask))
        << "Combo(Ah,Ks) should conflict with a board mask containing Ah";
}

TEST(ComboConflictsMask, NoConflictWithUnrelatedBoard) {
    Combo combo(C("Ah"), C("Ks"));
    uint64_t boardMask = uint64_t(1) << C("2c").value;
    EXPECT_FALSE(combo.conflicts(boardMask))
        << "Combo(Ah,Ks) should not conflict with a board mask containing only 2c";
}

TEST(ComboConflictsMask, EmptyMaskNoConflict) {
    Combo combo(C("Ah"), C("Ks"));
    EXPECT_FALSE(combo.conflicts(uint64_t(0)))
        << "Combo(Ah,Ks) should not conflict with an empty mask";
}

// ── operator== (ignores weight) ───────────────────────────────────────────────

TEST(ComboEquality, SameCardsEqualRegardlessOfWeight) {
    Combo c1(C("Ah"), C("Ks"), 1.0f);
    Combo c2(C("Ah"), C("Ks"), 0.5f);
    EXPECT_EQ(c1, c2)
        << "operator== should ignore weight: same cards = same combo";
}

TEST(ComboEquality, SameCardsReversedInputEqual) {
    Combo c1(C("Ah"), C("Ks"));
    Combo c2(C("Ks"), C("Ah")); // reversed — canonicalizes to same
    EXPECT_EQ(c1, c2);
}

TEST(ComboEquality, DifferentCardsNotEqual) {
    Combo c1(C("Ah"), C("Ks"));
    Combo c2(C("Ah"), C("Qd"));
    EXPECT_NE(c1, c2)
        << "Combo(Ah,Ks) != Combo(Ah,Qd)";
}

// ── operator< (canonical ordering for sort/dedup) ─────────────────────────────

TEST(ComboOrdering, LowerCardsLessThanHigherCards) {
    Combo low(C("2c"), C("3d"));
    Combo high(C("Ah"), C("Ks"));
    EXPECT_TRUE(low < high)
        << "Combo(2c,3d) should be less than Combo(Ah,Ks)";
    EXPECT_FALSE(high < low);
}

TEST(ComboOrdering, EqualCombosNotLess) {
    Combo c1(C("Ah"), C("Ks"));
    Combo c2(C("Ah"), C("Ks"));
    EXPECT_FALSE(c1 < c2);
    EXPECT_FALSE(c2 < c1);
}

// ── Death test: same card twice ───────────────────────────────────────────────

TEST(ComboDeathTest, SameCardTwiceAborts) {
    // assert(a.value != b.value) fires in debug builds.
    EXPECT_DEBUG_DEATH(Combo(C("Ah"), C("Ah")), "");
}

#include "equitycalc/Board.h"
#include <gtest/gtest.h>
#include <cstdint>

using equitycalc::Board;
using equitycalc::Card;
using equitycalc::Combo;

static Card C(const char* s) { return *Card::parse(s); }

// ── Empty board ───────────────────────────────────────────────────────────────

TEST(BoardEmpty, CountAndMaskAreZero) {
    Board b;
    EXPECT_EQ(b.count(), 0u);
    EXPECT_EQ(b.mask(), 0u);
}

TEST(BoardEmpty, ToStringIsEmpty) {
    Board b;
    EXPECT_EQ(b.toString(), "");
}

// ── addCard ───────────────────────────────────────────────────────────────────

TEST(BoardAddCard, CountIncrementsPerCard) {
    Board b;
    b.addCard(C("2c"));
    EXPECT_EQ(b.count(), 1u);
    b.addCard(C("7d"));
    EXPECT_EQ(b.count(), 2u);
    b.addCard(C("9s"));
    EXPECT_EQ(b.count(), 3u);
}

TEST(BoardAddCard, HandCountMatchesBoardCount) {
    Board b;
    b.addCard(C("2c"));
    b.addCard(C("7d"));
    b.addCard(C("9s"));
    EXPECT_EQ(b.hand().count(), b.count())
        << "omp::Hand.count() must match board.count()";
}

TEST(BoardAddCard, MaskHasExactlyNBitsSet) {
    Board b;
    b.addCard(C("2c"));
    b.addCard(C("7d"));
    b.addCard(C("9s"));
    EXPECT_EQ(__builtin_popcountll(b.mask()), 3)
        << "mask should have exactly 3 bits set for a 3-card board";
}

TEST(BoardAddCard, MaskBitsMatchCardValues) {
    Card tc = C("2c"), td = C("7d"), ts = C("9s");
    Board b;
    b.addCard(tc);
    b.addCard(td);
    b.addCard(ts);
    EXPECT_TRUE(b.mask() & (uint64_t(1) << tc.value)) << "bit for 2c must be set";
    EXPECT_TRUE(b.mask() & (uint64_t(1) << td.value)) << "bit for 7d must be set";
    EXPECT_TRUE(b.mask() & (uint64_t(1) << ts.value)) << "bit for 9s must be set";
}

// ── Death test: adding the same card twice ────────────────────────────────────

TEST(BoardDeathTest, DuplicateCardAborts) {
    Board b;
    b.addCard(C("Ah"));
    EXPECT_DEBUG_DEATH(b.addCard(C("Ah")), "");
}

// ── fromMask ──────────────────────────────────────────────────────────────────

TEST(BoardFromMask, CountMatchesBitCount) {
    uint64_t mask = (uint64_t(1) << C("2c").value)
                  | (uint64_t(1) << C("7d").value)
                  | (uint64_t(1) << C("9s").value);
    Board b = Board::fromMask(mask);
    EXPECT_EQ(b.count(), 3u);
    EXPECT_EQ(b.mask(), mask);
}

TEST(BoardFromMask, ToStringReproducesCards) {
    // Build mask for 2c 7d 9s; toString should reproduce them in value order.
    uint64_t mask = (uint64_t(1) << C("2c").value)
                  | (uint64_t(1) << C("7d").value)
                  | (uint64_t(1) << C("9s").value);
    Board b = Board::fromMask(mask);
    EXPECT_EQ(b.toString(), "2c 7d 9s");
}

TEST(BoardFromMask, EmptyMaskGivesEmptyBoard) {
    Board b = Board::fromMask(0);
    EXPECT_EQ(b.count(), 0u);
    EXPECT_EQ(b.mask(), 0u);
}

// ── withCard (non-mutating) ───────────────────────────────────────────────────

TEST(BoardWithCard, OriginalBoardUnchanged) {
    Board original;
    original.addCard(C("2c"));

    Board extended = original.withCard(C("7d"));

    EXPECT_EQ(original.count(), 1u) << "withCard must not mutate the original board";
    EXPECT_FALSE(original.mask() & (uint64_t(1) << C("7d").value))
        << "7d bit must not appear in original after withCard";
}

TEST(BoardWithCard, ReturnedBoardHasNewCard) {
    Board original;
    original.addCard(C("2c"));

    Board extended = original.withCard(C("7d"));

    EXPECT_EQ(extended.count(), 2u);
    EXPECT_TRUE(extended.mask() & (uint64_t(1) << C("7d").value));
}

// ── Flush detection — proves omp::Hand::empty() seeding ──────────────────────

TEST(BoardFlushDetection, FiveHeartsDetectedAsFlush) {
    // If evalHand_ were default-constructed instead of omp::Hand::empty(),
    // suit counters would be garbage and hasFlush() would be wrong.
    Board b;
    b.addCard(C("2h"));
    b.addCard(C("3h"));
    b.addCard(C("4h"));
    b.addCard(C("5h"));
    b.addCard(C("6h"));
    EXPECT_TRUE(b.hand().hasFlush())
        << "five hearts must trigger hasFlush() — fails if empty() seeding is missing";
}

TEST(BoardFlushDetection, FourHeartsNotFlush) {
    Board b;
    b.addCard(C("2h"));
    b.addCard(C("3h"));
    b.addCard(C("4h"));
    b.addCard(C("5h"));
    EXPECT_FALSE(b.hand().hasFlush());
}

// ── conflicts ─────────────────────────────────────────────────────────────────

TEST(BoardConflicts, SharedCardIsConflict) {
    Board b;
    b.addCard(C("2c"));
    Combo combo(C("2c"), C("3d"));
    EXPECT_TRUE(b.conflicts(combo))
        << "board containing 2c should conflict with Combo(2c,3d)";
}

TEST(BoardConflicts, NoSharedCardNoConflict) {
    Board b;
    b.addCard(C("2c"));
    Combo combo(C("Ah"), C("Ks"));
    EXPECT_FALSE(b.conflicts(combo));
}

TEST(BoardConflicts, EmptyBoardNoConflict) {
    Board b;
    Combo combo(C("Ah"), C("Ks"));
    EXPECT_FALSE(b.conflicts(combo));
}

// ── hand() accessor ───────────────────────────────────────────────────────────

TEST(BoardHand, CountMatchesAfterAdds) {
    Board b;
    b.addCard(C("Ah"));
    b.addCard(C("Ks"));
    b.addCard(C("Qd"));
    EXPECT_EQ(b.hand().count(), 3u);
}

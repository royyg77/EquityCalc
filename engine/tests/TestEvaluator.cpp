#include "equitycalc/Evaluator.h"
#include "equitycalc/Board.h"
#include "equitycalc/Combo.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace equitycalc;

// Build an omp::Hand from card values (rank*4+suit, suits s=0 h=1 c=2 d=3).
static omp::Hand makeHand(std::initializer_list<int> cards) {
    omp::Hand h = omp::Hand::empty();
    for (int v : cards)
        h += omp::Hand(v);
    return h;
}

// Reference card values used throughout:
//   Royal flush  (spades):  Ts=32  Js=36  Qs=40  Ks=44  As=48
//   Str. flush   (spades):  5s=12  6s=16  7s=20  8s=24  9s=28
//   Quads        (aces):    As=48  Ah=49  Ac=50  Ad=51  Ks=44
//   Full house   (AAA KK):  As=48  Ah=49  Ac=50  Ks=44  Kh=45
//   Flush        (hearts):  2h=1   5h=13  8h=25  Jh=37  Ah=49
//   Straight     (broadway, no flush): Ah=49 Kh=45 Qh=41 Jh=37 Tc=34
//   Trips:                  As=48  Ah=49  Ac=50  Ks=44  Qd=43
//   Two pair:               As=48  Ah=49  Ks=44  Kh=45  Qd=43
//   One pair:               As=48  Ah=49  Ks=44  Qd=43  Jh=37
//   High card:              As=48  Ks=44  Qd=43  Jh=37  9c=30

TEST(EvaluatorOrdering, StrictHandRankOrder) {
    Evaluator ev;

    uint16_t royalFlush    = ev.evaluate(makeHand({48, 44, 40, 36, 32}));
    uint16_t straightFlush = ev.evaluate(makeHand({28, 24, 20, 16, 12}));
    uint16_t quads         = ev.evaluate(makeHand({51, 50, 49, 48, 44}));
    uint16_t fullHouse     = ev.evaluate(makeHand({50, 49, 48, 45, 44}));
    uint16_t flush         = ev.evaluate(makeHand({49, 37, 25, 13,  1}));
    uint16_t straight      = ev.evaluate(makeHand({49, 45, 41, 37, 34}));
    uint16_t trips         = ev.evaluate(makeHand({50, 49, 48, 44, 43}));
    uint16_t twoPair       = ev.evaluate(makeHand({49, 48, 45, 44, 43}));
    uint16_t onePair       = ev.evaluate(makeHand({49, 48, 44, 43, 37}));
    uint16_t highCard      = ev.evaluate(makeHand({48, 44, 43, 37, 30}));

    EXPECT_GT(royalFlush,    straightFlush);
    EXPECT_GT(straightFlush, quads);
    EXPECT_GT(quads,         fullHouse);
    EXPECT_GT(fullHouse,     flush);
    EXPECT_GT(flush,         straight);
    EXPECT_GT(straight,      trips);
    EXPECT_GT(trips,         twoPair);
    EXPECT_GT(twoPair,       onePair);
    EXPECT_GT(onePair,       highCard);
}

TEST(EvaluatorCategory, FlushIsCategory6) {
    Evaluator ev;
    uint16_t s = ev.evaluate(makeHand({49, 37, 25, 13, 1}));
    EXPECT_EQ(Evaluator::category(s), HandCategory::Flush);
}

TEST(EvaluatorCategory, OnePairIsCategory2) {
    Evaluator ev;
    uint16_t s = ev.evaluate(makeHand({49, 48, 44, 43, 37}));
    EXPECT_EQ(Evaluator::category(s), HandCategory::OnePair);
}

TEST(EvaluatorCategory, AllCategoriesMapped) {
    Evaluator ev;
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({48, 44, 40, 36, 32}))), HandCategory::StraightFlush);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({28, 24, 20, 16, 12}))), HandCategory::StraightFlush);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({51, 50, 49, 48, 44}))), HandCategory::Quads);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({50, 49, 48, 45, 44}))), HandCategory::FullHouse);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({49, 37, 25, 13,  1}))), HandCategory::Flush);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({49, 45, 41, 37, 34}))), HandCategory::Straight);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({50, 49, 48, 44, 43}))), HandCategory::Trips);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({49, 48, 45, 44, 43}))), HandCategory::TwoPair);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({49, 48, 44, 43, 37}))), HandCategory::OnePair);
    EXPECT_EQ(Evaluator::category(ev.evaluate(makeHand({48, 44, 43, 37, 30}))), HandCategory::HighCard);
}

TEST(EvaluatorDeterminism, SameHandSameResult) {
    Evaluator ev;
    auto h = makeHand({49, 48, 44, 43, 37});
    EXPECT_EQ(ev.evaluate(h), ev.evaluate(h));
    EXPECT_EQ(ev.evaluate(h), ev.evaluate(h));
}

TEST(EvaluatorCardCounts, FiveCardHand) {
    Evaluator ev;
    EXPECT_NO_THROW(ev.evaluate(makeHand({48, 44, 40, 36, 32})));
}

TEST(EvaluatorCardCounts, SixCardHand) {
    Evaluator ev;
    EXPECT_NO_THROW(ev.evaluate(makeHand({48, 44, 40, 36, 32, 0})));
}

TEST(EvaluatorCardCounts, SevenCardHand) {
    Evaluator ev;
    EXPECT_NO_THROW(ev.evaluate(makeHand({48, 44, 40, 36, 32, 0, 4})));
}

TEST(EvaluatorCardCounts, SevenCardPicksBestFive) {
    Evaluator ev;
    // Best 5 from Ah Jh 8h 5h 2h Ks Qd is a flush (hearts).
    uint16_t s = ev.evaluate(makeHand({49, 37, 25, 13, 1, 44, 43}));
    EXPECT_EQ(Evaluator::category(s), HandCategory::Flush);
}

TEST(EvaluatorConvenience, ComboAndBoardOverload) {
    Evaluator ev;
    // Hole cards: Ah As. Board: Kh Qh Jh Th 2s.
    // Best 5: Ah Kh Qh Jh Th → royal flush.
    Combo c(Card(12, 1), Card(12, 0));  // Ah, As
    Board b;
    b.addCard(Card(11, 1));  // Kh
    b.addCard(Card(10, 1));  // Qh
    b.addCard(Card(9,  1));  // Jh
    b.addCard(Card(8,  1));  // Th
    b.addCard(Card(0,  0));  // 2s
    EXPECT_EQ(Evaluator::category(ev.evaluate(c, b)), HandCategory::StraightFlush);
}

TEST(EvaluatorConcurrency, SharedEvaluatorManyThreads) {
    Evaluator ev;
    auto h = makeHand({49, 37, 25, 13, 1});  // flush
    uint16_t expected = ev.evaluate(h);

    constexpr int kThreads = 8;
    constexpr int kIter    = 10'000;
    std::vector<std::thread> threads;
    std::vector<bool> ok(kThreads, true);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kIter; ++i) {
                if (ev.evaluate(h) != expected) {
                    ok[t] = false;
                    return;
                }
            }
        });
    }
    for (auto& th : threads) th.join();
    for (int t = 0; t < kThreads; ++t)
        EXPECT_TRUE(ok[t]) << "thread " << t << " got inconsistent result";
}

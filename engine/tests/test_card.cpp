#include "equitycalc/card.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <set>
#include <vector>

using equitycalc::Card;

// ── Encoding pin ──────────────────────────────────────────────────────────────

TEST(CardEncodingPin, AhIs49) {
    EXPECT_EQ(Card(12, 1).value, 49) << "Ah: rank=12 suit=1 → 12*4+1 = 49";
}

TEST(CardEncodingPin, TwoSIs0) {
    EXPECT_EQ(Card(0, 0).value, 0) << "2s: rank=0 suit=0 → 0*4+0 = 0";
}

// ── parse: known values ───────────────────────────────────────────────────────

TEST(CardParse, Ah) {
    auto c = Card::parse("Ah");
    ASSERT_TRUE(c.has_value()) << "input: \"Ah\"";
    EXPECT_EQ(c->rank(),  12) << "expected rank 12 (Ace)";
    EXPECT_EQ(c->suit(),   1) << "expected suit 1 (hearts, OMP order)";
    EXPECT_EQ(c->value,   49) << "expected value 49";
}

TEST(CardParse, TwoS) {
    auto c = Card::parse("2s");
    ASSERT_TRUE(c.has_value()) << "input: \"2s\"";
    EXPECT_EQ(c->rank(),  0) << "expected rank 0 (deuce)";
    EXPECT_EQ(c->suit(),  0) << "expected suit 0 (spades, OMP order)";
    EXPECT_EQ(c->value,   0) << "expected value 0";
}

TEST(CardParse, AdIsMax) {
    auto c = Card::parse("Ad");
    ASSERT_TRUE(c.has_value()) << "input: \"Ad\"";
    EXPECT_EQ(c->value, 51) << "Ad: rank=12 suit=3(d) → 12*4+3 = 51 (max)";
}

// ── Round-trip ────────────────────────────────────────────────────────────────

TEST(CardRoundTrip, AllFiftyTwoValues) {
    for (uint8_t v = 0; v < 52; ++v) {
        std::string s = Card(v).toString();
        auto rt = Card::parse(s);
        ASSERT_TRUE(rt.has_value())
            << "toString() produced unparseable string for value=" << (int)v
            << " (\"" << s << "\")";
        EXPECT_EQ(rt->value, v)
            << "value=" << (int)v << " → toString()=\"" << s
            << "\" → parse() → value=" << (int)rt->value;
    }
}

// ── Case-insensitivity ────────────────────────────────────────────────────────

TEST(CardCaseInsensitivity, SuitChar) {
    auto upper = Card::parse("AH");
    auto lower = Card::parse("ah");
    auto mixed = Card::parse("Ah");
    ASSERT_TRUE(upper.has_value()) << "input: \"AH\"";
    ASSERT_TRUE(lower.has_value()) << "input: \"ah\"";
    ASSERT_TRUE(mixed.has_value()) << "input: \"Ah\"";
    EXPECT_EQ(upper->value, 49) << "\"AH\" expected value 49";
    EXPECT_EQ(*upper, *lower)   << "\"AH\" != \"ah\"";
    EXPECT_EQ(*lower, *mixed)   << "\"ah\" != \"Ah\"";
}

TEST(CardCaseInsensitivity, RankCharT) {
    auto lower = Card::parse("th");
    auto upper = Card::parse("TH");
    ASSERT_TRUE(lower.has_value()) << "input: \"th\"";
    ASSERT_TRUE(upper.has_value()) << "input: \"TH\"";
    EXPECT_EQ(*lower, *upper) << "\"th\" != \"TH\"";
}

TEST(CardCaseInsensitivity, RankCharJ) {
    auto lower = Card::parse("jc");
    auto upper = Card::parse("JC");
    ASSERT_TRUE(lower.has_value()) << "input: \"jc\"";
    ASSERT_TRUE(upper.has_value()) << "input: \"JC\"";
    EXPECT_EQ(*lower, *upper) << "\"jc\" != \"JC\"";
}

// ── Rejects invalid input ─────────────────────────────────────────────────────

TEST(CardParseRejects, BadRankAndSuit) {
    EXPECT_FALSE(Card::parse("Zx").has_value()) << "input: \"Zx\" (bad rank and suit)";
}

TEST(CardParseRejects, TooShort) {
    EXPECT_FALSE(Card::parse("A").has_value()) << "input: \"A\" (1 char)";
}

TEST(CardParseRejects, TooLong) {
    EXPECT_FALSE(Card::parse("Ahh").has_value()) << "input: \"Ahh\" (3 chars)";
}

TEST(CardParseRejects, EmptyString) {
    EXPECT_FALSE(Card::parse("").has_value()) << "input: \"\" (empty)";
}

TEST(CardParseRejects, InvalidRankChar) {
    EXPECT_FALSE(Card::parse("1h").has_value()) << "input: \"1h\" ('1' not a rank)";
}

TEST(CardParseRejects, InvalidSuitChar) {
    EXPECT_FALSE(Card::parse("Ax").has_value()) << "input: \"Ax\" ('x' not a suit)";
}

// ── All 52 distinct values 0–51 ───────────────────────────────────────────────

TEST(CardCoverage, FiftyTwoDistinctValues) {
    std::set<uint8_t> seen;
    const char* rank_chars = "23456789TJQKA";
    const char* suit_chars = "shcd"; // OMP order: s=0 h=1 c=2 d=3
    for (int r = 0; r < 13; ++r) {
        for (int s = 0; s < 4; ++s) {
            char buf[3] = {rank_chars[r], suit_chars[s], '\0'};
            auto c = Card::parse(buf);
            ASSERT_TRUE(c.has_value()) << "failed to parse \"" << buf << "\"";
            ASSERT_TRUE(c->isValid())  << "isValid() false for \"" << buf << "\"";
            seen.insert(c->value);
        }
    }
    EXPECT_EQ(seen.size(), 52u) << "expected 52 distinct values";
    for (uint8_t v = 0; v < 52; ++v) {
        EXPECT_EQ(seen.count(v), 1u) << "value " << (int)v << " missing or duplicated";
    }
}

// ── Ordering: rank-major ──────────────────────────────────────────────────────

TEST(CardOrdering, TwoSLessThanAceS) {
    auto two_s = Card::parse("2s");
    auto ace_s = Card::parse("As");
    ASSERT_TRUE(two_s.has_value());
    ASSERT_TRUE(ace_s.has_value());
    EXPECT_LT(two_s->value, ace_s->value)
        << "2s.value=" << (int)two_s->value
        << "  As.value=" << (int)ace_s->value;
    EXPECT_TRUE(*two_s < *ace_s);
}

TEST(CardOrdering, SortRestoresValueOrder) {
    std::vector<Card> all;
    all.reserve(52);
    for (uint8_t v = 0; v < 52; ++v) all.push_back(Card(v));

    auto shuffled = all;
    std::reverse(shuffled.begin(), shuffled.end());
    std::sort(shuffled.begin(), shuffled.end());

    for (size_t i = 0; i < 52; ++i) {
        EXPECT_EQ(shuffled[i].value, all[i].value)
            << "position " << i << ": got value=" << (int)shuffled[i].value
            << " expected=" << (int)all[i].value;
    }
}

TEST(CardOrdering, RankMajorWithinSorted) {
    std::vector<Card> sorted;
    sorted.reserve(52);
    for (uint8_t v = 0; v < 52; ++v) sorted.push_back(Card(v));

    for (size_t i = 1; i < sorted.size(); ++i) {
        EXPECT_TRUE(sorted[i-1] < sorted[i])
            << "positions " << i-1 << " and " << i << " not strictly ascending";
        if (sorted[i-1].rank() == sorted[i].rank()) {
            EXPECT_LT(sorted[i-1].suit(), sorted[i].suit())
                << "same rank=" << (int)sorted[i].rank()
                << " but suit did not increment at position " << i;
        } else {
            EXPECT_LT(sorted[i-1].rank(), sorted[i].rank())
                << "rank decreased at position " << i;
        }
    }
}

// ── isValid ───────────────────────────────────────────────────────────────────

TEST(CardIsValid, BoundaryValues) {
    EXPECT_TRUE( Card(0).isValid())   << "Card(0) should be valid";
    EXPECT_TRUE( Card(51).isValid())  << "Card(51) should be valid";
    EXPECT_FALSE(Card(52).isValid())  << "Card(52) should be invalid (out of range)";
    EXPECT_FALSE(Card(255).isValid()) << "Card(255) should be invalid";
}

// ── Comparison operators ──────────────────────────────────────────────────────

TEST(CardComparison, AllOperators) {
    Card a(0), b(0), c(1);
    // a=Card(0), b=Card(0), c=Card(1)
    EXPECT_TRUE( a == b) << "Card(0) == Card(0)";
    EXPECT_FALSE(a == c) << "Card(0) != Card(1)";
    EXPECT_TRUE( a != c) << "Card(0) != Card(1)";
    EXPECT_TRUE( a <  c) << "Card(0) < Card(1)";
    EXPECT_TRUE( c >  a) << "Card(1) > Card(0)";
    EXPECT_TRUE( a <= b) << "Card(0) <= Card(0)";
    EXPECT_TRUE( a <= c) << "Card(0) <= Card(1)";
    EXPECT_TRUE( c >= b) << "Card(1) >= Card(0)";
}

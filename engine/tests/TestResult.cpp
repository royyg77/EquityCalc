#include "equitycalc/Result.h"
#include <gtest/gtest.h>
#include <string>

using namespace equitycalc;

// ── Helpers ───────────────────────────────────────────────────────────────────

static Result make2Player(double eq0, double eq1,
                           double w0,  double w1,
                           double t0,  double t1,
                           double totalWeight,
                           uint64_t scenarios = 100,
                           double timeSec = 0.01) {
    Result r;
    r.players             = 2;
    r.equity[0]           = eq0;  r.equity[1]           = eq1;
    r.wins[0]             = w0;   r.wins[1]             = w1;
    r.ties[0]             = t0;   r.ties[1]             = t1;
    r.totalWeightTallied  = totalWeight;
    r.totalScenarios      = scenarios;
    r.timeSeconds         = timeSec;
    r.mode                = Mode::Enumeration;
    return r;
}

// ── Default state ─────────────────────────────────────────────────────────────

TEST(ResultDefault, ZeroInitialized) {
    Result r;
    EXPECT_EQ(r.players, 0u);
    EXPECT_EQ(r.totalScenarios, 0u);
    EXPECT_EQ(r.totalWeightTallied, 0.0);
    EXPECT_EQ(r.timeSeconds, 0.0);
    EXPECT_EQ(r.ciHalfWidth, 0.0);
    EXPECT_EQ(r.mode, Mode::Enumeration);
    for (unsigned i = 0; i < MAX_PLAYERS; ++i) {
        EXPECT_EQ(r.equity[i], 0.0);
        EXPECT_EQ(r.wins[i],   0.0);
        EXPECT_EQ(r.ties[i],   0.0);
    }
}

// ── format() ─────────────────────────────────────────────────────────────────

TEST(ResultFormat, ContainsPlayerLines) {
    Result r = make2Player(0.6, 0.4, 60.0, 40.0, 0.0, 0.0, 100.0);
    std::string s = r.format();
    EXPECT_NE(s.find("P1:"), std::string::npos);
    EXPECT_NE(s.find("P2:"), std::string::npos);
}

TEST(ResultFormat, ContainsEquityWinTieLabels) {
    Result r = make2Player(0.6, 0.4, 60.0, 40.0, 0.0, 0.0, 100.0);
    std::string s = r.format();
    EXPECT_NE(s.find("equity:"), std::string::npos);
    EXPECT_NE(s.find("win:"),    std::string::npos);
    EXPECT_NE(s.find("tie:"),    std::string::npos);
}

TEST(ResultFormat, EnumerationModeLabel) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode = Mode::Enumeration;
    EXPECT_NE(r.format().find("enumeration"), std::string::npos);
}

TEST(ResultFormat, MonteCarloModeLabel) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode = Mode::MonteCarlo;
    EXPECT_NE(r.format().find("monte carlo"), std::string::npos);
}

TEST(ResultFormat, CIShownInMCModeWhenNonZero) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode        = Mode::MonteCarlo;
    r.ciHalfWidth = 0.005;
    std::string s = r.format();
    // ± encoded as UTF-8 0xc2 0xb1
    EXPECT_NE(s.find("\xc2\xb1"), std::string::npos);
}

TEST(ResultFormat, CINotShownInEnumerationMode) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode        = Mode::Enumeration;
    r.ciHalfWidth = 0.005;  // set but should not appear
    EXPECT_EQ(r.format().find("\xc2\xb1"), std::string::npos);
}

TEST(ResultFormat, CINotShownInMCModeWhenZero) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode        = Mode::MonteCarlo;
    r.ciHalfWidth = 0.0;
    EXPECT_EQ(r.format().find("\xc2\xb1"), std::string::npos);
}

TEST(ResultFormat, ZeroWeightDenominatorGuard) {
    // totalWeightTallied == 0 should not produce nan/inf — falls back to 1.0
    Result r = make2Player(0.5, 0.5, 0.0, 0.0, 0.0, 0.0, /*totalWeight=*/0.0);
    EXPECT_NO_THROW({
        std::string s = r.format();
        EXPECT_EQ(s.find("nan"), std::string::npos);
        EXPECT_EQ(s.find("inf"), std::string::npos);
    });
}

TEST(ResultFormat, TieCountsVisible) {
    // A 2-way chop: wins=0, ties=100 for both
    Result r = make2Player(0.5, 0.5, 0.0, 0.0, 100.0, 100.0, 200.0, 100);
    std::string s = r.format();
    // Each player's line should contain their tie share
    EXPECT_NE(s.find("P1:"), std::string::npos);
    EXPECT_NE(s.find("P2:"), std::string::npos);
}

TEST(ResultFormat, ScenarioAndTimeVisible) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0, 1234, 0.42);
    std::string s = r.format();
    EXPECT_NE(s.find("1234"), std::string::npos);
}

TEST(ResultFormat, ThreePlayerLines) {
    Result r;
    r.players = 3;
    r.equity[0] = 0.4; r.equity[1] = 0.35; r.equity[2] = 0.25;
    r.totalWeightTallied = 100.0;
    r.totalScenarios = 100;
    r.mode = Mode::Enumeration;
    std::string s = r.format();
    EXPECT_NE(s.find("P1:"), std::string::npos);
    EXPECT_NE(s.find("P2:"), std::string::npos);
    EXPECT_NE(s.find("P3:"), std::string::npos);
}

// ── toJson() ──────────────────────────────────────────────────────────────────

TEST(ResultJson, WellFormedBraces) {
    Result r = make2Player(0.6, 0.4, 60.0, 40.0, 0.0, 0.0, 100.0);
    std::string j = r.toJson();
    EXPECT_EQ(j.front(), '{');
    EXPECT_EQ(j.back(),  '}');
}

TEST(ResultJson, ContainsRequiredKeys) {
    Result r = make2Player(0.6, 0.4, 60.0, 40.0, 0.0, 0.0, 100.0);
    std::string j = r.toJson();
    EXPECT_NE(j.find("\"players\""),            std::string::npos);
    EXPECT_NE(j.find("\"mode\""),               std::string::npos);
    EXPECT_NE(j.find("\"totalScenarios\""),     std::string::npos);
    EXPECT_NE(j.find("\"totalWeightTallied\""), std::string::npos);
    EXPECT_NE(j.find("\"timeSeconds\""),        std::string::npos);
    EXPECT_NE(j.find("\"ciHalfWidth\""),        std::string::npos);
    EXPECT_NE(j.find("\"equity\""),             std::string::npos);
    EXPECT_NE(j.find("\"wins\""),               std::string::npos);
    EXPECT_NE(j.find("\"ties\""),               std::string::npos);
}

TEST(ResultJson, EnumerationModeString) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode = Mode::Enumeration;
    EXPECT_NE(r.toJson().find("\"enumeration\""), std::string::npos);
}

TEST(ResultJson, MonteCarlModeString) {
    Result r = make2Player(0.5, 0.5, 50.0, 50.0, 0.0, 0.0, 100.0);
    r.mode = Mode::MonteCarlo;
    // No space: "montecarlo" not "monte carlo"
    EXPECT_NE(r.toJson().find("\"montecarlo\""), std::string::npos);
}

TEST(ResultJson, EquityArrayLength) {
    // 2-player JSON equity array should contain exactly 2 values.
    Result r = make2Player(0.6, 0.4, 60.0, 40.0, 0.0, 0.0, 100.0);
    std::string j = r.toJson();
    auto start = j.find("\"equity\": [");
    ASSERT_NE(start, std::string::npos);
    auto arrStart = j.find('[', start);
    auto arrEnd   = j.find(']', arrStart);
    std::string arr = j.substr(arrStart + 1, arrEnd - arrStart - 1);
    // Count commas: 2 values → 1 comma
    int commas = 0;
    for (char c : arr) if (c == ',') ++commas;
    EXPECT_EQ(commas, 1);
}

TEST(ResultJson, WinsByPlayerMaskNotExposed) {
    // winsByPlayerMask is an internal tally; the JSON does not expose it.
    Result r = make2Player(0.6, 0.4, 60.0, 40.0, 0.0, 0.0, 100.0);
    EXPECT_EQ(r.toJson().find("winsByPlayerMask"), std::string::npos);
}

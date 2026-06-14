// engine/bench/benchmark.cpp
//
// Standardized cross-version benchmark + correctness anchor for EquityCalc.
//
// Spots are grouped into three tiers by ACTUAL WORK (scenario count =
// matchups x runouts-per-matchup), NOT by which mode they happen to trigger:
//
//   SMALL   < ~2M scenarios     instant      sanity + the AA/KK anchor
//   MEDIUM  ~40M-80M scenarios  sub-second   mode mix: auto-enum + auto-MC
//   LARGE   > ~100M scenarios   seconds      the optimization targets
//
// Mode is not a tier property -- it falls out of the 50M enumThreshold:
// a spot under the threshold auto-selects exact enumeration, a spot over it
// auto-selects Monte Carlo. forceExact raises enumThreshold to UINT64_MAX so a
// large spot stays exact instead of switching to MC; this is how the heavy
// enumerator-stress spots (the v1.1 threading target) are kept exact.
//
// The L1/L2 pair runs identical ranges as exact vs MC to compare enumerator
// throughput against the sampler on the same input. L3 is a wide MC-only spot.
//
// Each spot also checks its result against a golden equity (when set), so an
// optimization that is faster but wrong FAILS instead of looking like a win.
//
// Usage:
//   benchmark [--reps N] [--csv out.csv] [--filter substr] [--list]
//
// Compare versions:
//   ./build/benchmark --csv v1.0.csv         # baseline
//   ./build/benchmark --csv v1.1.csv         # after optimizing (rebuild first)
//   python3 bench/compare.py v1.0.csv v1.1.csv

#include "equitycalc/Parser.h"
#include "equitycalc/Range.h"
#include "equitycalc/Board.h"
#include "equitycalc/Calculator.h"
#include "equitycalc/Result.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace equitycalc;

uint64_t parseCardMask(std::string_view s) {
    uint64_t mask = 0;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        Card c = parseCard(s.substr(i, 2));
        mask |= uint64_t{1} << c.value;
    }
    return mask;
}

// -- Spot definition ---------------------------------------------------------
struct Spot {
    std::string              name;        // stable CSV key -- never rename
    std::vector<std::string> ranges;
    std::string              board;       // "" = preflop
    std::string              dead;        // "" = none
    Mode                     expectMode;  // mode the spot SHOULD select
    bool                     forceExact;  // raise enumThreshold so it stays exact
    std::vector<double>      golden;      // expected per-player equity; {} = no check
    double                   tol;         // absolute equity tolerance
};

// ADD spots; don't edit existing ones (editing invalidates baseline
// comparisons). The real scenario count is reported at runtime from the
// engine's Result.totalScenarios -- the comments below are descriptive only.
std::vector<Spot> spots() {
    return {
        // -- SMALL -- instant; sanity + the canonical anchor -----------------

        // Complete board: exactly 1 scenario per matchup, no runout recursion.
        {   "S1_river_specific",
            {"AhKs", "QdQc"}, "2h7s9dTcJh", "",
            Mode::Enumeration, false, {0.0, 1.0}, 0.0 },

        // THE anchor. AA vs KK preflop. Golden = verified engine output.
        {   "S2_AhAs_vs_KhKs",
            {"AhAs", "KhKs"}, "", "",
            Mode::Enumeration, false, {0.826366, 0.173634}, 0.0005 },

        // Weighted normalization guard: AA + half-weighted KK vs a fixed hand.
        {   "S3_weighted_norm",
            {"AhAs,KhKs:0.5", "QdQc"}, "", "",
            Mode::Enumeration, false, {}, 0.0 },

        // -- MEDIUM -- ~40M-80M scenarios; mode falls out of the 50M threshold

        // Two larger ranges on a flop, sized to stay UNDER the 50M threshold so
        // it remains exact. 222 vs 198 combos, ~43.5M scenarios. Stresses
        // decodeMatchup + inter-player conflict checks at high matchup count.
        {   "M1_wide_vs_wide_flop",
            {"22+,A2s+,A7o+", "22+,A2s+,A9o+,KQo"}, "2h7s9d", "",
            Mode::Enumeration, false, {}, 0.0 },

        // QQ vs JJ preflop, ~61.6M scenarios. Over the 50M threshold, so it is
        // FORCED exact to stay an enumerator spot (otherwise it would auto-MC).
        // Enumerator stress at medium work.
        {   "M2_QQ_vs_JJ_enum",
            {"QQ", "JJ"}, "", "",
            Mode::Enumeration, true, {}, 0.0 },

        // QQ vs JJ preflop, ~61.6M scenarios. Same ranges as M2 but unforced,
        // so it auto-selects MC. Lets you compare the MC estimate against M2's
        // exact result on identical input.
        {   "M3_QQ_vs_JJ_MC",
            {"QQ", "JJ"}, "", "",
            Mode::MonteCarlo, false, {}, 0.0 },

        // Two larger ranges on a flop, sized to cross the 50M threshold so it
        // auto-switches to Monte Carlo. 286 vs 262 combos, ~74.2M scenarios.
        // Gives the medium tier a sampler path at moderate work. Seeded.
        {   "M4_wide_vs_wide_MC_flop",
            {"22+,A2s+,A2o+,K9s+", "22+,A2s+,A2o+,76s,87s,98s,T9s"}, "2h7s9d", "",
            Mode::MonteCarlo, false, {}, 0.0 },

        // -- LARGE -- > ~100M scenarios; the optimization targets -------------

        // Forced-exact, ~3.1B scenarios. Heaviest exact spot -- the wall-time
        // multithreading should slash in v1.1+. Several seconds single-threaded.
        // forceExact is REQUIRED: unforced this auto-switches to Monte Carlo.
        {   "L1_TTp_AQsp_vs_99p_AJsp_exact",
            {"TT+,AQs+", "99+,AJs+"}, "", "",
            Mode::Enumeration, true, {}, 0.0 },

        // Same ranges as L1, run as Monte Carlo (auto-selected, since ~3.1B
        // scenarios is far past the 50M threshold). Pairs with L1 to compare
        // sampler throughput against the exact enumerator on identical input.
        {   "L2_TTp_AQsp_vs_99p_AJsp_MC",
            {"TT+,AQs+", "99+,AJs+"}, "", "",
            Mode::MonteCarlo, false, {}, 0.0 },

        // Monte Carlo path: wide vs wide, far past threshold -> auto-MC.
        // Seeded for reproducibility. Throughput here is samples/sec.
        {   "L3_wide_vs_wide_MC",
            {"22+,A2s+,A5o+,K9s+,KTo+,Q9s+,QTo+,J9s+,JTo,T9s",
             "22+,A2s+,A2o+,K2s+,K5o+,Q5s+,Q9o+,J7s+,J9o+,T8s+,98s,87s"},
            "", "",
            Mode::MonteCarlo, false, {}, 0.0 },
    };
}

CalculatorConfig makeConfig(const Spot& s) {
    CalculatorConfig cfg;
    cfg.threadCount = 1;
    if (s.forceExact)
        cfg.enumThreshold = UINT64_MAX;      // never auto-switch to MC
    if (s.expectMode == Mode::MonteCarlo) {
        cfg.mcCiTarget = 1e-4;
        cfg.mcSeed     = 0x5EED12345678ULL;  // reproducible
    }
    return cfg;
}

std::vector<Range> parseRanges(const std::vector<std::string>& strs) {
    std::vector<Range> out;
    out.reserve(strs.size());
    for (const auto& s : strs) out.push_back(parseRange(s));
    return out;
}

// -- Timing ------------------------------------------------------------------
struct Timing {
    double minSec, medianSec;
    uint64_t scenarios;
    double throughput;
    Mode mode;
    Result sample;
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n ? (n % 2 ? v[n/2] : 0.5*(v[n/2-1]+v[n/2])) : 0.0;
}

Timing runSpot(const Spot& s, unsigned reps) {
    auto baseRanges = parseRanges(s.ranges);
    uint64_t boardMask = s.board.empty() ? 0 : parseCardMask(s.board);
    uint64_t deadMask  = s.dead.empty()  ? 0 : parseCardMask(s.dead);
    Board board = boardMask ? Board::fromMask(boardMask) : Board{};
    CalculatorConfig cfg = makeConfig(s);

    std::vector<double> times;
    times.reserve(reps);
    Result last{};

    for (unsigned r = 0; r < reps; ++r) {
        auto ranges = baseRanges;            // compute() mutates ranges
        Calculator calc(std::move(ranges), board, deadMask, cfg);
        auto t0 = std::chrono::steady_clock::now();
        last = calc.compute();
        auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double>(t1 - t0).count());
    }

    double mn = *std::min_element(times.begin(), times.end());
    Timing t;
    t.minSec     = mn;
    t.medianSec  = median(times);
    t.scenarios  = last.totalScenarios;
    t.throughput = mn > 0.0 ? double(last.totalScenarios) / mn : 0.0;
    t.mode       = last.mode;
    t.sample     = last;
    return t;
}

// -- Correctness -------------------------------------------------------------
enum class Check { Pass, Fail, TimingOnly, ModeMismatch };

Check checkResult(const Spot& s, const Timing& t) {
    if (t.mode != s.expectMode) return Check::ModeMismatch;
    if (s.golden.empty())       return Check::TimingOnly;
    if (s.golden.size() != t.sample.players) return Check::Fail;
    for (unsigned i = 0; i < t.sample.players; ++i)
        if (std::fabs(t.sample.equity[i] - s.golden[i]) > s.tol)
            return Check::Fail;
    return Check::Pass;
}

const char* checkStr(Check c) {
    switch (c) {
        case Check::Pass: return "PASS";
        case Check::Fail: return "FAIL";
        case Check::TimingOnly: return "time";
        case Check::ModeMismatch: return "MODE!";
    }
    return "?";
}

// -- Reporting ---------------------------------------------------------------
void printHeader() {
    std::cout << std::left  << std::setw(34) << "spot"
              << std::right << std::setw(6)  << "mode"
              << std::setw(11) << "min(s)"
              << std::setw(11) << "med(s)"
              << std::setw(16) << "scenarios"
              << std::setw(15) << "scen/s"
              << std::setw(7)  << "check" << "\n"
              << std::string(100, '-') << "\n";
}

void printRow(const Spot& s, const Timing& t, Check c) {
    std::cout << std::left  << std::setw(34) << s.name
              << std::right << std::setw(6)
              << (t.mode == Mode::Enumeration ? "enum" : "mc")
              << std::setw(11) << std::fixed << std::setprecision(5) << t.minSec
              << std::setw(11) << t.medianSec
              << std::setw(16) << t.scenarios
              << std::setw(15) << std::setprecision(0) << t.throughput
              << std::setw(7)  << checkStr(c) << "\n";
}

void writeCsvRow(std::ostream& os, const Spot& s, const Timing& t, Check c) {
    os << s.name << ','
       << (t.mode == Mode::Enumeration ? "enum" : "mc") << ','
       << std::fixed << std::setprecision(6) << t.minSec << ','
       << t.medianSec << ',' << t.scenarios << ','
       << std::setprecision(1) << t.throughput << ',' << checkStr(c);
    os << std::setprecision(6);
    for (unsigned i = 0; i < t.sample.players; ++i)
        os << ',' << t.sample.equity[i];
    os << '\n';
}

} // namespace



int main(int argc, char** argv) {
    unsigned reps = 5;
    std::string csvPath, filter;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--reps"   && i+1 < argc) reps = std::stoul(argv[++i]);
        else if (a == "--csv"    && i+1 < argc) csvPath = argv[++i];
        else if (a == "--filter" && i+1 < argc) filter = argv[++i];
        else if (a == "--list")                 listOnly = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "benchmark [--reps N] [--csv out.csv] "
                         "[--filter substr] [--list]\n";
            return 0;
        }
    }

    auto all = spots();
    if (listOnly) {
        for (const auto& s : all)
            std::cout << s.name << "\n";
        return 0;
    }

    printHeader();

    std::ofstream csv;
    if (!csvPath.empty()) {
        csv.open(csvPath);
        if (!csv.is_open()) {
            std::cerr << "error: cannot open CSV path '" << csvPath
                      << "' (does the directory exist?)\n";
            return 2;
        }
        csv << "spot,mode,min_s,median_s,scenarios,scen_per_s,check,equities...\n";
    }

    int failures = 0;
    for (const auto& s : all) {
        if (!filter.empty() && s.name.find(filter) == std::string::npos) continue;
        try {
            Timing t = runSpot(s, reps);
            Check  c = checkResult(s, t);
            printRow(s, t, c);
            if (csv.is_open()) writeCsvRow(csv, s, t, c);
            if (c == Check::Fail || c == Check::ModeMismatch) ++failures;
        } catch (const std::exception& e) {
            std::cout << std::left << std::setw(34) << s.name
                      << "  ERROR: " << e.what() << "\n";
            ++failures;
        }
    }

    if (!csvPath.empty())
        std::cout << "\nCSV written to " << csvPath << "\n";

    if (failures) {
        std::cout << "\n" << failures << " spot(s) failed.\n";
        return 1;
    }
    return 0;
}



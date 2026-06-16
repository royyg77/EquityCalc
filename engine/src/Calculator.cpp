#include "equitycalc/Calculator.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <omp/Util.h>
#include <random>
#include <stdexcept>

namespace equitycalc {

namespace {
    constexpr unsigned  kMcBatchSize  = 4096;
    constexpr uint64_t  kMcMaxSamples = 100'000'000;
    constexpr unsigned  kMcMaxRetries = 1000;
} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────

Calculator::Calculator(std::vector<Range> ranges, Board board,
                       uint64_t deadMask, CalculatorConfig cfg)
    : ranges_(std::move(ranges))
    , board_(board)
    , deadMask_(deadMask)
    , cfg_(cfg)
{
    // assert(ranges_.size() >= 2 && ranges_.size() <= MAX_PLAYERS);
    if (ranges_.size() < 2 || ranges_.size() > MAX_PLAYERS)
        throw std::invalid_argument(
            "Calculator requires 2.." + std::to_string(MAX_PLAYERS) +
            " ranges; got " + std::to_string(ranges_.size()));
}

// ── Setup + dispatch ──────────────────────────────────────────────────────────

Result Calculator::compute() {
    unsigned n = static_cast<unsigned>(ranges_.size());

    // Step 1: Remove combos that conflict with the fixed board or dead cards.
    uint64_t fixedMask = board_.mask() | deadMask_;
    for (auto& r : ranges_) r.removeConflicts(fixedMask);

    // Step 2: Snapshot range sizes; reject empty ranges.
    N_ = 1;
    for (unsigned i = 0; i < n; ++i) {
        rangeSizes_[i] = ranges_[i].size();
        if (rangeSizes_[i] == 0)
            throw std::runtime_error(
                "player range is empty after board/dead conflict removal");
        // Overflow-safe multiply: saturate at UINT64_MAX (will force MC).
        if (N_ > UINT64_MAX / rangeSizes_[i]) { N_ = UINT64_MAX; break; }
        N_ *= rangeSizes_[i];
    }

    // Steps 3–5: Estimate total scenarios in floating point to avoid overflow.
    unsigned boardCount = board_.count();
    unsigned remaining  = (boardCount >= 5) ? 0u : (5u - boardCount);
    // fix: remove GCC/Clang intrinsics and replace with omp wrappers from Util.h
    // unsigned fixedCards = static_cast<unsigned>(__builtin_popcountll(fixedMask));
    unsigned fixedCards = static_cast<unsigned>(omp::bitCount(fixedMask));
    unsigned deckEst    = (fixedCards + 2u * n < 52u)
                          ? (52u - fixedCards - 2u * n) : 0u;

    double runoutEst = 1.0;
    for (unsigned i = 0; i < remaining; ++i)
        runoutEst = runoutEst
                  * static_cast<double>(deckEst > i ? deckEst - i : 1u)
                  / static_cast<double>(i + 1);

    bool useMC = (static_cast<double>(N_) * runoutEst
                  >= static_cast<double>(cfg_.enumThreshold));

    if (!useMC) return computeExact();

    buildSamplers();  // Step 6 (MC only): build weighted combo samplers
    return computeMonteCarlo();
}

// ── Exact driver ──────────────────────────────────────────────────────────────

Result Calculator::computeExact() {
    auto t0 = std::chrono::steady_clock::now();
    unsigned n = static_cast<unsigned>(ranges_.size());
    BatchAccumulator acc;

    omp::Hand playerHands[MAX_PLAYERS];
    unsigned  comboIdx[MAX_PLAYERS];

    for (uint64_t p = 0; p < N_; ++p) {
        decodeMatchup(p, comboIdx);

        uint64_t usedMask = board_.mask() | deadMask_;
        double   weight   = 1.0;
        bool     ok       = true;

        for (unsigned i = 0; i < n; ++i) {
            const Combo& c = ranges_[i].combos()[comboIdx[i]];
            if (c.cardMask & usedMask) { ok = false; break; }
            usedMask       |= c.cardMask;
            playerHands[i]  = c.evalHand;
            weight         *= static_cast<double>(c.weight);
        }
        if (!ok) continue;

        runOutBoard(playerHands, n, usedMask, weight, acc);
    }

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    return buildResult(acc, n, elapsed, Mode::Enumeration);
}

// ── Monte Carlo driver ────────────────────────────────────────────────────────

Result Calculator::computeMonteCarlo() {
    auto t0 = std::chrono::steady_clock::now();
    unsigned n = static_cast<unsigned>(ranges_.size());
    BatchAccumulator acc;

    // std::mt19937_64 rng(std::random_device{}());
    std::mt19937_64 rng(cfg_.mcSeed != 0
                        ? cfg_.mcSeed
                        : std::random_device{}());

    std::uniform_int_distribution<int> cardDist(0, 51);

    // Pre-build per-range distributions over [0, totalWeight).
    std::uniform_real_distribution<double> rangeDist[MAX_PLAYERS];
    for (unsigned i = 0; i < n; ++i)
        rangeDist[i] = std::uniform_real_distribution<double>(
            0.0, cumulative_[i].back());

    unsigned boardCount = board_.count();
    unsigned remaining  = (boardCount >= 5) ? 0u : (5u - boardCount);

    double   ciHalfWidth = 1.0;
    omp::Hand playerHands[MAX_PLAYERS];

    while (acc.scenariosScored < kMcMaxSamples) {
        // Run a fixed-size batch of attempts.
        for (unsigned iter = 0; iter < kMcBatchSize; ++iter) {
            // Sample a valid matchup; retry on inter-player conflict.
            uint64_t usedMask = board_.mask() | deadMask_;
            bool ok = false;

            for (unsigned attempt = 0; attempt < kMcMaxRetries; ++attempt) {
                usedMask = board_.mask() | deadMask_;
                ok = true;
                for (unsigned i = 0; i < n; ++i) {
                    unsigned idx   = drawCombo(i, rangeDist[i](rng));
                    const Combo& c = ranges_[i].combos()[idx];
                    if (c.cardMask & usedMask) { ok = false; break; }
                    usedMask       |= c.cardMask;
                    playerHands[i]  = c.evalHand;
                }
                if (ok) break;
            }
            if (!ok) continue;  // pathological range — skip this attempt

            // Sample one runout: draw `remaining` distinct cards by rejection.
            omp::Hand boardHand = board_.hand();
            uint64_t  m         = usedMask;
            for (unsigned r = 0; r < remaining; ++r) {
                int c;
                do { c = cardDist(rng); } while (m & (uint64_t{1} << c));
                m         |= (uint64_t{1} << c);
                boardHand  = boardHand + omp::Hand(c);
            }

            // Weight = 1.0: weighting already lives in drawCombo (weighted sampling).
            // Do NOT multiply combo weights here — that would double-apply them.
            scoreScenario(playerHands, n, boardHand, 1.0, acc);
        }

        // Check convergence at batch boundary.
        ciHalfWidth = computeCiHalfWidth(acc, n);
        if (ciHalfWidth <= cfg_.mcCiTarget) break;
    }

    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    Result r = buildResult(acc, n, elapsed, Mode::MonteCarlo);
    r.ciHalfWidth = ciHalfWidth;
    return r;
}

// ── Inner-loop helpers ────────────────────────────────────────────────────────

void Calculator::decodeMatchup(uint64_t p, unsigned* comboIdx) const noexcept {
    unsigned n = static_cast<unsigned>(ranges_.size());
    for (unsigned i = 0; i < n; ++i) {
        comboIdx[i] = static_cast<unsigned>(p % rangeSizes_[i]);
        p           = p / rangeSizes_[i];
    }
}

void Calculator::runOutBoard(const omp::Hand* playerHands, unsigned n,
                              uint64_t usedMask, double weight,
                              BatchAccumulator& acc) const {
    unsigned boardCount = board_.count();
    unsigned remaining  = (boardCount >= 5) ? 0u : (5u - boardCount);

    if (remaining == 0) {
        scoreScenario(playerHands, n, board_.hand(), weight, acc);
        return;
    }

    // Build the available-card deck once per matchup (Decision 2).
    // recurseBoard receives this array and never rebuilds it.
    uint8_t  deck[52];
    unsigned ndeck = 0;
    for (int c = 0; c < 52; ++c)
        if (!(usedMask & (uint64_t{1} << c)))
            deck[ndeck++] = static_cast<uint8_t>(c);

    recurseBoard(playerHands, n, board_.hand(), deck, ndeck, 0, remaining, weight, acc);
}

void Calculator::recurseBoard(const omp::Hand* playerHands, unsigned n,
                               const omp::Hand& boardHand,
                               const uint8_t* deck, unsigned ndeck,
                               unsigned start, unsigned remaining,
                               double weight, BatchAccumulator& acc) const {
    if (remaining == 0) {
        scoreScenario(playerHands, n, boardHand, weight, acc);
        return;
    }
    // start = i+1 at each level gives combinations, not permutations:
    // each 5-card board is visited exactly once.
    for (unsigned i = start; i < ndeck; ++i) {
        omp::Hand next = boardHand + omp::Hand(deck[i]);
        recurseBoard(playerHands, n, next, deck, ndeck, i + 1, remaining - 1, weight, acc);
    }
}

void Calculator::scoreScenario(const omp::Hand* playerHands, unsigned n,
                                const omp::Hand& board, double weight,
                                BatchAccumulator& acc) const noexcept {
    uint16_t best       = 0;
    uint32_t winnerMask = 0;

    for (unsigned i = 0; i < n; ++i) {
        uint16_t rank = eval_.evaluate(playerHands[i] + board);
        if (rank > best) {
            best       = rank;
            winnerMask = (1u << i);
        } else if (rank == best) {
            winnerMask |= (1u << i);
        }
    }

    // Decision 4 contract: numerator and denominator in lockstep.
    // No caller can tally a scenario without its weight entering totalWeightTallied.
    acc.winsByPlayerMask[winnerMask] += weight;
    acc.totalWeightTallied           += weight;
    acc.scenariosScored              += 1;
}

// ── MC samplers ───────────────────────────────────────────────────────────────

void Calculator::buildSamplers() {
    unsigned n = static_cast<unsigned>(ranges_.size());
    for (unsigned i = 0; i < n; ++i) {
        const auto& combos = ranges_[i].combos();
        cumulative_[i].resize(combos.size());
        double running = 0.0;
        for (size_t k = 0; k < combos.size(); ++k) {
            running           += static_cast<double>(combos[k].weight);
            cumulative_[i][k]  = running;
        }
    }
}

unsigned Calculator::drawCombo(unsigned rangeIdx, double u) const {
    const auto& cum = cumulative_[rangeIdx];
    auto it = std::lower_bound(cum.begin(), cum.end(), u);
    if (it == cum.end()) --it;  // guard: floating-point u may equal cum.back()
    return static_cast<unsigned>(it - cum.begin());
}

// ── Finalization ──────────────────────────────────────────────────────────────

Result Calculator::buildResult(const BatchAccumulator& acc, unsigned n,
                                double timeSeconds, Mode mode) const {
    Result r;
    r.players = n;
    r.mode    = mode;

    // Derive per-player wins, ties, and pre-normalized equity from the tally.
    for (unsigned mask = 1; mask < (1u << n); ++mask) {
        double w = acc.winsByPlayerMask[mask];
        if (w == 0.0) continue;
        // fix: remove GCC/Clang intrinsics and replace with omp wrappers from Util.h
        // unsigned k     = static_cast<unsigned>(__builtin_popcount(mask));
        unsigned k     = static_cast<unsigned>(omp::bitCount(mask));

        double   share = w / static_cast<double>(k);
        for (unsigned i = 0; i < n; ++i) {
            if (!(mask & (1u << i))) continue;
            r.equity[i] += share;         // accumulate before division
            if (k == 1) r.wins[i] += w;   // outright win: full weight
            else        r.ties[i] += share; // chop: 1/k each
        }
    }

    // Decision 4 normalization: divide by the denominator accumulated in
    // scoreScenario — never by scenario count or a precomputed product.
    if (acc.totalWeightTallied > 0.0)
        for (unsigned i = 0; i < n; ++i)
            r.equity[i] /= acc.totalWeightTallied;

    r.totalWeightTallied = acc.totalWeightTallied;
    r.totalScenarios     = acc.scenariosScored;
    r.timeSeconds        = timeSeconds;
    std::memcpy(r.winsByPlayerMask, acc.winsByPlayerMask, sizeof r.winsByPlayerMask);
    return r;
}

double Calculator::computeCiHalfWidth(const BatchAccumulator& acc,
                                       unsigned n) noexcept {
    if (acc.scenariosScored == 0) return 1.0;
    double N = static_cast<double>(acc.scenariosScored);
    double maxHW = 0.0;
    for (unsigned i = 0; i < n; ++i) {
        double eq = 0.0;
        for (unsigned mask = 1; mask < (1u << n); ++mask) {
            if (!(mask & (1u << i))) continue;
            double w = acc.winsByPlayerMask[mask];
            if (w == 0.0) continue;
            // fix: remove GCC/Clang intrinsics and replace with omp wrappers from Util.h
            // unsigned k = static_cast<unsigned>(__builtin_popcount(mask));
            unsigned k = static_cast<unsigned>(omp::bitCount(mask));

            eq += w / static_cast<double>(k);
        }
        eq /= N;
        double hw = 1.96 * std::sqrt(eq * (1.0 - eq) / N);
        maxHW = std::max(maxHW, hw);
    }
    return maxHW;
}

} // namespace equitycalc

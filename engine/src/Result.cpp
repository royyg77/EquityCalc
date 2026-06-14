#include "equitycalc/Result.h"
#include <iomanip>
#include <sstream>

namespace equitycalc {

// std::string Result::format() const {
//     std::ostringstream os;
//     os << std::fixed;

//     for (unsigned i = 0; i < players; ++i) {
//         os << "P" << (i + 1) << ": "
//            << std::setw(8) << std::setprecision(4) << equity[i] * 100.0 << "%"
//            << "   wins: " << static_cast<uint64_t>(wins[i])
//            << "   ties: " << std::setprecision(1) << ties[i]
//            << '\n';
//     }

//     os << totalScenarios << " scenarios in "
//        << std::setprecision(2) << timeSeconds << "s"
//        << " (" << (mode == Mode::Enumeration ? "enumeration" : "monte carlo") << ")";

//     if (mode == Mode::MonteCarlo && ciHalfWidth > 0.0) {
//         os << "  (\xc2\xb1" << std::setprecision(4) << ciHalfWidth * 100.0 << "%)";
//     }

//     return os.str();
// }

std::string Result::format() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);

    double denom = (totalWeightTallied > 0.0) ? totalWeightTallied : 1.0;

    for (unsigned i = 0; i < players; ++i) {
        double winPct    = wins[i]   / denom * 100.0;
        double tiePct    = ties[i]   / denom * 100.0;
        double equityPct = equity[i] * 100.0;

        os << "P" << (i + 1) << ": " << std::setprecision(4)
           << "equity: " << std::setw(8) << equityPct << "%"
           << "   win: " << std::setw(8) << winPct << "% (" 
                << static_cast<uint64_t>(wins[i]) << ")"
           << "   tie: " << std::setw(8) << tiePct << "% (" 
                << std::setprecision(1) << ties[i] << ")"
           << '\n';
    }

    os << totalScenarios << " scenarios in "
       << std::setprecision(2) << timeSeconds << "s"
       << " (" << (mode == Mode::Enumeration ? "enumeration" : "monte carlo") << ")";

    if (mode == Mode::MonteCarlo && ciHalfWidth > 0.0) {
        os << "  (\xc2\xb1" << std::setprecision(4) << ciHalfWidth * 100.0 << "%)";
    }

    return os.str();
}

// std::string Result::toJson() const {
//     // TODO: use a JSON library or manual serialization.
//     // Fields: players, equity[], wins[], ties[], totalScenarios, totalWeightTallied,
//     //         timeSeconds, mode, ciHalfWidth.
//     std::ostringstream os;
//     os << "{\n";
//     os << "  \"players\": " << players << ",\n";
//     os << "  \"mode\": \"" << (mode == Mode::Enumeration ? "enumeration" : "montecarlo") << "\",\n";
//     os << "  \"totalScenarios\": " << totalScenarios << ",\n";
//     os << "  \"timeSeconds\": " << std::fixed << std::setprecision(4) << timeSeconds << ",\n";
//     os << "  \"ciHalfWidth\": " << std::setprecision(6) << ciHalfWidth << ",\n";
//     os << "  \"equity\": [";
//     for (unsigned i = 0; i < players; ++i) {
//         if (i) os << ", ";
//         os << std::setprecision(6) << equity[i];
//     }
//     os << "],\n";
//     os << "  \"wins\": [";
//     for (unsigned i = 0; i < players; ++i) {
//         if (i) os << ", ";
//         os << wins[i];
//     }
//     os << "],\n";
//     os << "  \"ties\": [";
//     for (unsigned i = 0; i < players; ++i) {
//         if (i) os << ", ";
//         os << std::setprecision(4) << ties[i];
//     }
//     os << "]\n}";
//     return os.str();
// }

std::string Result::toJson() const {
    // Emits raw values (fractions and counts), NOT formatted percentages.
    // Consumers (frontend, scripts) compute their own display:
    //   winPct = wins[i] / totalWeightTallied * 100
    // Manual serialization is fine for this fixed, flat shape; swap in a
    // JSON library only if the structure grows or escaping becomes an issue.
    std::ostringstream os;
    os << std::fixed;

    os << "{\n";
    os << "  \"players\": " << players << ",\n";
    os << "  \"mode\": \"" << (mode == Mode::Enumeration ? "enumeration" : "montecarlo") << "\",\n";
    os << "  \"totalScenarios\": " << totalScenarios << ",\n";
    os << "  \"totalWeightTallied\": " << std::setprecision(6) << totalWeightTallied << ",\n";
    os << "  \"timeSeconds\": " << std::setprecision(4) << timeSeconds << ",\n";
    os << "  \"ciHalfWidth\": " << std::setprecision(6) << ciHalfWidth << ",\n";

    os << "  \"equity\": [";
    for (unsigned i = 0; i < players; ++i) {
        if (i) os << ", ";
        os << std::setprecision(6) << equity[i];
    }
    os << "],\n";

    os << "  \"wins\": [";
    for (unsigned i = 0; i < players; ++i) {
        if (i) os << ", ";
        os << std::setprecision(6) << wins[i];
    }
    os << "],\n";

    os << "  \"ties\": [";
    for (unsigned i = 0; i < players; ++i) {
        if (i) os << ", ";
        os << std::setprecision(6) << ties[i];
    }
    os << "]\n}";
    return os.str();
}

} // namespace equitycalc

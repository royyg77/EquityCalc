#include "equitycalc/Result.h"
#include <iomanip>
#include <sstream>

namespace equitycalc {

std::string Result::format() const {
    std::ostringstream os;
    os << std::fixed;

    for (unsigned i = 0; i < players; ++i) {
        os << "P" << (i + 1) << ": "
           << std::setw(6) << std::setprecision(2) << equity[i] * 100.0 << "%"
           << "   wins: " << static_cast<uint64_t>(wins[i])
           << "   ties: " << std::setprecision(1) << ties[i]
           << '\n';
    }

    os << totalScenarios << " scenarios in "
       << std::setprecision(2) << timeSeconds << "s"
       << " (" << (mode == Mode::Enumeration ? "enumeration" : "monte carlo") << ")";

    if (mode == Mode::MonteCarlo && ciHalfWidth > 0.0) {
        os << "  (\xc2\xb1" << std::setprecision(2) << ciHalfWidth * 100.0 << "%)";
    }

    return os.str();
}

std::string Result::toJson() const {
    // TODO: use a JSON library or manual serialization.
    // Fields: players, equity[], wins[], ties[], totalScenarios, totalWeightTallied,
    //         timeSeconds, mode, ciHalfWidth.
    std::ostringstream os;
    os << "{\n";
    os << "  \"players\": " << players << ",\n";
    os << "  \"mode\": \"" << (mode == Mode::Enumeration ? "enumeration" : "montecarlo") << "\",\n";
    os << "  \"totalScenarios\": " << totalScenarios << ",\n";
    os << "  \"timeSeconds\": " << std::fixed << std::setprecision(4) << timeSeconds << ",\n";
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
        os << wins[i];
    }
    os << "],\n";
    os << "  \"ties\": [";
    for (unsigned i = 0; i < players; ++i) {
        if (i) os << ", ";
        os << std::setprecision(4) << ties[i];
    }
    os << "]\n}";
    return os.str();
}

} // namespace equitycalc

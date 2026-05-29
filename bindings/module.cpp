// pybind11 module exposing the EquityCalc engine to Python.
//
// Status: stub. Currently exposes one trivial function (`version()`) so the
// build can be verified end-to-end. As engine types come online (Card, Range,
// EquityCalculator), add their bindings here.

#include <pybind11/pybind11.h>
#include <string>

namespace py = pybind11;

namespace {
std::string version() {
    return "0.1.0-dev";
}
}  // namespace

PYBIND11_MODULE(_equitycalc, m) {
    m.doc() = "EquityCalc C++ engine bindings";
    m.def("version", &version, "Return the engine version string.");

    // TODO: bind Card, Rank, Suit, Range, Board, EquityCalculator, Result.
}

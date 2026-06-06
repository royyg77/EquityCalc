#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view kVersion = "0.1.0-dev";
}

int main(int argc, char** argv) {
    if (argc > 1) {
        std::string_view arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            std::cout << "equitycalc " << kVersion << "\n";
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: equitycalc [--version] [--help]\n"
                      << "  (calculation flags coming soon)\n";
            return 0;
        }
    }
    std::cout << "equitycalc " << kVersion << " — ready.\n";
    return 0;
}

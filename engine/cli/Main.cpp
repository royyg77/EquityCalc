
#include "equitycalc/Parser.h"
#include "equitycalc/Range.h"
#include "equitycalc/Board.h"
#include "equitycalc/Calculator.h"
#include "equitycalc/Result.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace equitycalc;

[[noreturn]] void usage(const char* prog, int code) {
    std::cerr <<
        "Usage: " << prog << " <range1> <range2> [range3 ... range6]\n"
        "                  [--board <cards>] [--dead <cards>] [--json]\n"
        "\n"
        "  ranges   2-6 hand-range strings, e.g. \"AhKs\" \"QQ+,AKs:0.5\"\n"
        "  --board  fixed community cards, e.g. --board Ah7d2c\n"
        "  --dead   removed (dead) cards, e.g. --dead KdKc\n"
        "  --json   emit JSON instead of the text table\n";
    std::exit(code);
}

// Parse a concatenated card string ("Ah7d2c") into a 52-bit mask.
// Every two characters is one card; throws on malformed input.
uint64_t parseCardMask(std::string_view s, const char* what) {
    if (s.size() % 2 != 0)
        throw std::runtime_error(std::string(what) +
            ": card string has odd length (each card is 2 chars)");
    uint64_t mask = 0;
    for (size_t i = 0; i < s.size(); i += 2) {
        Card c = parseCard(s.substr(i, 2));   // throws ParseError if invalid
        uint64_t bit = uint64_t{1} << c.value;
        if (mask & bit)
            throw std::runtime_error(std::string(what) +
                ": duplicate card '" + c.toString() + "'");
        mask |= bit;
    }
    return mask;
}

Board boardFromMask(uint64_t mask) {
    return mask ? Board::fromMask(mask) : Board{};
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> rangeStrs;
    std::string boardStr, deadStr;
    bool json = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            usage(argv[0], 0);
        } else if (a == "--json") {
            json = true;
        } else if (a == "--board") {
            if (++i >= argc) { std::cerr << "--board needs an argument\n"; return 2; }
            boardStr = argv[i];
        } else if (a == "--dead") {
            if (++i >= argc) { std::cerr << "--dead needs an argument\n"; return 2; }
            deadStr = argv[i];
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown option: " << a << "\n";
            usage(argv[0], 2);
        } else {
            rangeStrs.push_back(std::move(a));
        }
    }

    if (rangeStrs.size() < 2 || rangeStrs.size() > MAX_PLAYERS) {
        std::cerr << "error: need 2.." << MAX_PLAYERS
                  << " ranges, got " << rangeStrs.size() << "\n";
        usage(argv[0], 2);
    }

    try {
        std::vector<Range> ranges;
        ranges.reserve(rangeStrs.size());
        for (const auto& s : rangeStrs)
            ranges.push_back(parseRange(s));   // throws ParseError on bad input

        uint64_t boardMask = boardStr.empty() ? 0 : parseCardMask(boardStr, "--board");
        uint64_t deadMask  = deadStr.empty()  ? 0 : parseCardMask(deadStr,  "--dead");

        if (boardMask & deadMask) {
            std::cerr << "error: a card appears in both --board and --dead\n";
            return 2;
        }

        Board board = boardFromMask(boardMask);

        Calculator calc(std::move(ranges), board, deadMask);
        Result r = calc.compute();

        std::cout << (json ? r.toJson() : r.format()) << "\n";
        return 0;
    }
    catch (const ParseError& e) {
        std::cerr << "parse error at position " << e.position
                  << ": " << e.what() << "\n";
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}

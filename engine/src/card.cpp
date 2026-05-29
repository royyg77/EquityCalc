#include "equitycalc/card.h"

namespace equitycalc {

namespace {

// Rank chars → 0–12. Returns 0xFF on unknown char.
constexpr uint8_t charToRank(char c) noexcept {
    switch (c) {
    case '2': return 0;  case '3': return 1;  case '4': return 2;
    case '5': return 3;  case '6': return 4;  case '7': return 5;
    case '8': return 6;  case '9': return 7;  case 't': return 8;
    case 'j': return 9;  case 'q': return 10; case 'k': return 11;
    case 'a': return 12;
    default:  return 0xFF;
    }
}

// Suit chars → 0–3 (OMP order: s/h/c/d). Returns 0xFF on unknown char.
constexpr uint8_t charToSuit(char c) noexcept {
    switch (c) {
    case 's': return 0;
    case 'h': return 1;
    case 'c': return 2;
    case 'd': return 3;
    default:  return 0xFF;
    }
}

constexpr char rankChars[] = "23456789TJQKA";
constexpr char suitChars[] = "shcd"; // OMP order

} // namespace

std::optional<Card> Card::parse(std::string_view s) {
    if (s.size() != 2) return std::nullopt;

    auto toLower = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    };

    uint8_t r = charToRank(toLower(s[0]));
    uint8_t suit = charToSuit(toLower(s[1]));
    if (r == 0xFF || suit == 0xFF) return std::nullopt;

    return Card{static_cast<uint8_t>((r << 2) | suit)};
}

std::string Card::toString() const {
    std::string out(2, '\0');
    out[0] = rankChars[rank()];
    out[1] = suitChars[suit()];
    return out;
}

} // namespace equitycalc

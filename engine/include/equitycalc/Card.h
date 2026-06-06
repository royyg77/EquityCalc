#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Card encoding contract (must match omp::Hand's card index):
//   value = rank * 4 + suit   (equivalently: (rank << 2) | suit)
//   rank : 0=2 ... 12=A
//   suit : s=0, h=1, c=2, d=3  (OMP order — NOT alphabetical)
//
// Changing the encoding does not produce a compile error; it silently produces
// wrong equities. Do not change without also changing every OMP boundary site.

namespace equitycalc {

class Card {
public:
    uint8_t value;

    // Trusting constructors — caller guarantees validity. Internal use only.
    explicit constexpr Card(uint8_t v) noexcept : value(v) {}
    explicit constexpr Card(uint8_t rank, uint8_t suit) noexcept
        : value(static_cast<uint8_t>((rank << 2) | suit)) {}

    // No default constructor: every Card that exists is a real card.
    Card() = delete;

    // Validating entry point for untrusted input ("Ah", "2s", …).
    // Returns nullopt for any malformed or out-of-range string.
    static std::optional<Card> parse(std::string_view s);

    constexpr uint8_t rank() const noexcept { return value >> 2; }
    constexpr uint8_t suit() const noexcept { return value & 3; }
    constexpr bool isValid() const noexcept { return value < 52; }

    // Produces strings like "Ah", "2s". Suits printed in OMP order (s/h/c/d).
    std::string toString() const;

    constexpr bool operator==(Card o) const noexcept { return value == o.value; }
    constexpr bool operator!=(Card o) const noexcept { return value != o.value; }
    // Rank-major: rank in high bits of value, so value comparison == rank-then-suit order.
    constexpr bool operator<(Card o) const noexcept { return value < o.value; }
    constexpr bool operator>(Card o) const noexcept { return value > o.value; }
    constexpr bool operator<=(Card o) const noexcept { return value <= o.value; }
    constexpr bool operator>=(Card o) const noexcept { return value >= o.value; }
};

// Encoding pin — these must hold or the OMP boundary is broken.
static_assert(Card(12, 1).value == 49, "Ah must be value 49");
static_assert(Card(0, 0).value == 0,   "2s must be value 0");

}  // namespace equitycalc

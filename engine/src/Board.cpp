#include "equitycalc/Board.h"
#include <cassert>
// #include <sstream>

namespace equitycalc {

Board::Board()
    : evalHand_(omp::Hand::empty())
    , cardMask_(0)
{}

Board Board::fromMask(uint64_t mask) {
    Board b;
    for (int i = 0; i < 52; ++i) {
        if (mask & (uint64_t{1} << i)) {
            b.addCard(Card(static_cast<uint8_t>(i)));
        }
    }
    return b;
}

void Board::addCard(Card c) {
    assert(!(cardMask_ & (uint64_t{1} << c.value)) && "Board: card already present");
    evalHand_ += omp::Hand(c.value);
    cardMask_  |= (uint64_t{1} << c.value);
}

Board Board::withCard(Card c) const {
    Board b = *this;
    b.addCard(c);
    return b;
}

std::string Board::toString() const {
    std::string out;
    bool first = true;
    for (int i = 0; i < 52; ++i) {
        if (cardMask_ & (uint64_t{1} << i)) {
            if (!first) out += ' ';
            out += Card(static_cast<uint8_t>(i)).toString();
            first = false;
        }
    }
    return out;
}

} // namespace equitycalc

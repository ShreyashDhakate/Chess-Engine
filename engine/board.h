#pragma once
#include <string>

#include "types.h"

namespace gambit {

class Board {
public:
    PieceCode sq[64] = {};
    Color stm = WHITE;
    uint8_t castling = 0;
    Square ep = NO_SQ;
    uint16_t halfmove = 0;
    uint16_t fullmove = 1;

    void setStart();
    bool setFen(const std::string& fen);
    std::string fen() const;

    Square kingSq(Color c) const;

    // Is `s` attacked by any piece of colour `by`? Radiates outward from `s`
    // rather than scanning all 64 squares.
    bool attacked(Square s, Color by) const;

    bool inCheck(Color c) const { return attacked(kingSq(c), ~c); }

    void makeMove(const Move& m, Undo& u);
    void unmakeMove(const Move& m, const Undo& u);
};

std::string toUci(const Move& m);

}  // namespace gambit

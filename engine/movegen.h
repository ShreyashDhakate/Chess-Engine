#pragma once
#include "board.h"
#include "types.h"

namespace gambit {

// 218 is the highest legal-move count known for any reachable position; 256 is
// a safe stack-allocated bound.
struct MoveList {
    Move moves[256];
    int size = 0;
    void add(const Move& m) { moves[size++] = m; }
    const Move* begin() const { return moves; }
    const Move* end() const { return moves + size; }
};

// Fills `out` with fully legal moves for the side to move.
void genLegal(Board& b, MoveList& out);

}  // namespace gambit

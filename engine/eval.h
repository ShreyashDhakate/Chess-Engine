#pragma once
#include "board.h"

namespace gambit {

// Centipawns, always from the point of view of the side to move (negamax).
int evaluate(const Board& b);

// Centipawn value of a piece type, used by eval and by MVV-LVA ordering.
int pieceValue(PieceType t);

}  // namespace gambit

#pragma once
#include <cstdint>
#include <cstdlib>

namespace gambit {

// Squares are a1=0, b1=1, ... h1=7, a2=8, ... h8=63.
// White pawns advance by +8, black pawns by -8.
using Square = int;
constexpr Square NO_SQ = -1;

enum Color : int { WHITE = 0, BLACK = 1 };
inline Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };

// board squares hold a signed piece code: 0 empty, +1..+6 white, -1..-6 black.
using PieceCode = int8_t;
constexpr PieceCode EMPTY = 0;

inline PieceCode encode(Color c, PieceType t) { return PieceCode(c == WHITE ? int(t) : -int(t)); }
inline PieceType typeOf(PieceCode p) { return PieceType(std::abs(int(p))); }
inline Color colorOf(PieceCode p) { return p > 0 ? WHITE : BLACK; }

constexpr int fileOf(Square s) { return s & 7; }
constexpr int rankOf(Square s) { return s >> 3; }
constexpr Square makeSq(int f, int r) { return r * 8 + f; }
constexpr bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

// Castling rights bitmask.
enum CastleRight : uint8_t { WK = 1, WQ = 2, BK = 4, BQ = 8 };

// A move needs an explicit flag: en-passant and castling both touch a square
// that is neither `from` nor `to`, so they cannot be inferred at unmake time.
enum MoveFlag : uint8_t { F_QUIET = 0, F_DOUBLE = 1, F_EP = 2, F_CASTLE = 3 };

struct Move {
    uint8_t from = 0;
    uint8_t to = 0;
    uint8_t promo = 0;  // PieceType of the promoted piece, or 0
    uint8_t flag = F_QUIET;
};

// State that make() destroys and unmake() must restore.
struct Undo {
    PieceCode captured = EMPTY;
    Square capturedSq = NO_SQ;  // differs from move.to on en-passant
    uint8_t castling = 0;
    Square ep = NO_SQ;
    uint16_t halfmove = 0;
};

}  // namespace gambit

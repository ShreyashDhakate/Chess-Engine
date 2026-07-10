#pragma once
#include <cstdint>

#include "board.h"
#include "movegen.h"

namespace gambit {

constexpr int MATE = 30000;
constexpr int MAX_PLY = 64;

// A score at or beyond this magnitude encodes a forced mate.
inline bool isMateScore(int s) { return s > MATE - MAX_PLY || s < -MATE + MAX_PLY; }
// Plies to mate, for reporting. Only meaningful when isMateScore(s).
inline int mateDistance(int s) { return s > 0 ? MATE - s : -MATE - s; }

struct SearchLimits {
    int maxDepth = 6;
    int movetimeMs = 0;  // 0 = no time cap, search maxDepth exactly

    // Ablation switches. The benchmark flips these to measure what each
    // technique is actually worth; production always leaves them on.
    bool ordering = true;
    bool quiescence = true;
};

struct SearchResult {
    Move best{};
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
    int ms = 0;
    bool hasMove = false;
};

enum class GameStatus { Ongoing, Check, Checkmate, Stalemate, DrawFiftyMove, DrawMaterial };

GameStatus status(Board& b);

SearchResult search(Board& b, const SearchLimits& limits);

}  // namespace gambit

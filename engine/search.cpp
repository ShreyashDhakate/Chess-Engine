#include "search.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "eval.h"

namespace gambit {

namespace {

using Clock = std::chrono::steady_clock;

struct Context {
    const SearchLimits* lim;
    Clock::time_point start;
    uint64_t nodes = 0;
    bool stopped = false;

    Move killers[MAX_PLY][2]{};
    int history[2][64][64]{};

    bool outOfTime() {
        if (lim->movetimeMs <= 0) return false;
        // Polling the clock per node is itself measurable; every 2048 is plenty.
        if ((nodes & 2047) != 0) return false;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        return ms >= lim->movetimeMs;
    }
};

bool sameMove(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.promo == b.promo;
}

bool isCapture(const Board& b, const Move& m) {
    return b.sq[m.to] != EMPTY || m.flag == F_EP;
}

// MVV-LVA: prefer taking the most valuable victim with the least valuable
// attacker. A pawn capturing a queen is searched long before a queen capturing
// a pawn, which is what makes alpha-beta cut off early.
int mvvLva(const Board& b, const Move& m) {
    const PieceType victim = (m.flag == F_EP) ? PAWN : typeOf(b.sq[m.to]);
    const PieceType attacker = typeOf(b.sq[m.from]);
    return 10 * pieceValue(victim) - pieceValue(attacker);
}

int scoreMove(const Board& b, const Move& m, const Context& ctx, int ply, const Move& pv, bool havePv) {
    if (havePv && sameMove(m, pv)) return 2000000;
    if (isCapture(b, m)) return 1000000 + mvvLva(b, m);
    if (m.promo) return 900000 + pieceValue(PieceType(m.promo));
    if (sameMove(m, ctx.killers[ply][0])) return 800000;
    if (sameMove(m, ctx.killers[ply][1])) return 700000;
    return ctx.history[b.stm][m.from][m.to];
}

// Selection sort, one pick per iteration. A full sort would rank moves we never
// look at, because a beta cutoff usually lands in the first few.
void pickBest(Move* moves, int* scores, int n, int i) {
    int bestIdx = i;
    for (int j = i + 1; j < n; ++j)
        if (scores[j] > scores[bestIdx]) bestIdx = j;
    std::swap(moves[i], moves[bestIdx]);
    std::swap(scores[i], scores[bestIdx]);
}

int quiesce(Board& b, int alpha, int beta, int ply, Context& ctx) {
    ++ctx.nodes;
    if (ctx.outOfTime()) {
        ctx.stopped = true;
        return 0;
    }

    // Stand pat: we are not obliged to capture, so a quiet position that is
    // already good enough produces a cutoff without searching further.
    const int standPat = evaluate(b);
    if (standPat >= beta) return beta;
    if (standPat > alpha) alpha = standPat;
    if (ply >= MAX_PLY - 1) return alpha;

    MoveList ml;
    genLegal(b, ml);

    Move moves[256];
    int scores[256];
    int n = 0;
    for (const Move& m : ml) {
        if (!isCapture(b, m) && !m.promo) continue;  // captures and promotions only
        moves[n] = m;
        scores[n] = mvvLva(b, m) + (m.promo ? pieceValue(PieceType(m.promo)) : 0);
        ++n;
    }

    Undo u;
    for (int i = 0; i < n; ++i) {
        pickBest(moves, scores, n, i);
        b.makeMove(moves[i], u);
        const int sc = -quiesce(b, -beta, -alpha, ply + 1, ctx);
        b.unmakeMove(moves[i], u);
        if (ctx.stopped) return 0;
        if (sc >= beta) return beta;
        if (sc > alpha) alpha = sc;
    }
    return alpha;
}

int negamax(Board& b, int depth, int alpha, int beta, int ply, Context& ctx, const Move& pv, bool havePv) {
    if (ctx.outOfTime()) {
        ctx.stopped = true;
        return 0;
    }

    MoveList ml;
    genLegal(b, ml);

    if (ml.size == 0) {
        // Mate scores shrink with distance so the search prefers a mate in 2
        // over a mate in 5. Stalemate is a draw, not a loss -- v1 scored it as
        // -99999 and would walk into stalemate to "avoid" losing.
        if (b.inCheck(b.stm)) return -MATE + ply;
        return 0;
    }
    if (b.halfmove >= 100) return 0;

    if (depth <= 0) {
        if (!ctx.lim->quiescence) {
            ++ctx.nodes;
            return evaluate(b);
        }
        return quiesce(b, alpha, beta, ply, ctx);
    }

    ++ctx.nodes;

    Move moves[256];
    int scores[256];
    const int n = ml.size;
    for (int i = 0; i < n; ++i) {
        moves[i] = ml.moves[i];
        scores[i] = ctx.lim->ordering ? scoreMove(b, moves[i], ctx, ply, pv, havePv) : 0;
    }

    Undo u;
    int best = -MATE * 2;
    for (int i = 0; i < n; ++i) {
        if (ctx.lim->ordering) pickBest(moves, scores, n, i);
        const Move m = moves[i];
        const bool quiet = !isCapture(b, m) && !m.promo;
        const Color us = b.stm;

        b.makeMove(m, u);
        const int sc = -negamax(b, depth - 1, -beta, -alpha, ply + 1, ctx, pv, false);
        b.unmakeMove(m, u);
        if (ctx.stopped) return 0;

        if (sc > best) best = sc;
        if (sc > alpha) alpha = sc;
        if (alpha >= beta) {
            if (quiet) {
                // Killers: a quiet move that refuted a sibling will often refute
                // this one too. History generalises that across the whole tree.
                if (!sameMove(m, ctx.killers[ply][0])) {
                    ctx.killers[ply][1] = ctx.killers[ply][0];
                    ctx.killers[ply][0] = m;
                }
                ctx.history[us][m.from][m.to] += depth * depth;
            }
            break;
        }
    }
    return best;
}

}  // namespace

GameStatus status(Board& b) {
    MoveList ml;
    genLegal(b, ml);
    if (ml.size == 0) return b.inCheck(b.stm) ? GameStatus::Checkmate : GameStatus::Stalemate;
    if (b.halfmove >= 100) return GameStatus::DrawFiftyMove;

    // Insufficient material: K vs K, K+minor vs K, K+B vs K+B.
    int minors = 0, others = 0;
    for (Square s = 0; s < 64; ++s) {
        const PieceCode p = b.sq[s];
        if (p == EMPTY) continue;
        switch (typeOf(p)) {
            case KING: break;
            case KNIGHT:
            case BISHOP: ++minors; break;
            default: ++others; break;
        }
    }
    if (others == 0 && minors <= 1) return GameStatus::DrawMaterial;

    return b.inCheck(b.stm) ? GameStatus::Check : GameStatus::Ongoing;
}

SearchResult search(Board& b, const SearchLimits& limits) {
    Context ctx;
    ctx.lim = &limits;
    ctx.start = Clock::now();

    SearchResult res;
    Move pv{};
    bool havePv = false;

    // Iterative deepening. Each pass is cheap relative to the next, and the best
    // move it finds is searched first next time -- which is most of why ordering
    // works at all. A pass cut short by the clock is discarded, not used.
    for (int depth = 1; depth <= limits.maxDepth; ++depth) {
        MoveList ml;
        genLegal(b, ml);
        if (ml.size == 0) break;

        Move moves[256];
        int scores[256];
        const int n = ml.size;
        for (int i = 0; i < n; ++i) {
            moves[i] = ml.moves[i];
            scores[i] = limits.ordering ? scoreMove(b, moves[i], ctx, 0, pv, havePv) : 0;
        }

        int alpha = -MATE * 2, beta = MATE * 2;
        Move bestThisDepth{};
        int bestScore = -MATE * 2;
        Undo u;

        for (int i = 0; i < n; ++i) {
            if (limits.ordering) pickBest(moves, scores, n, i);
            b.makeMove(moves[i], u);
            const int sc = -negamax(b, depth - 1, -beta, -alpha, 1, ctx, pv, havePv && i == 0);
            b.unmakeMove(moves[i], u);
            if (ctx.stopped) break;
            if (sc > bestScore) {
                bestScore = sc;
                bestThisDepth = moves[i];
            }
            if (sc > alpha) alpha = sc;
        }

        if (ctx.stopped) break;

        pv = bestThisDepth;
        havePv = true;
        res.best = bestThisDepth;
        res.score = bestScore;
        res.depth = depth;
        res.hasMove = true;

        if (isMateScore(bestScore)) break;  // nothing deeper to learn
    }

    res.nodes = ctx.nodes;
    res.ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - ctx.start).count());
    return res;
}

}  // namespace gambit

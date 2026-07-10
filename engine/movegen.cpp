#include "movegen.h"

namespace gambit {

namespace {

constexpr int KNIGHT_D[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KING_D[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
constexpr int BISHOP_D[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
constexpr int ROOK_D[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

void addPawnMove(MoveList& out, Square from, Square to, bool promoting, uint8_t flag) {
    if (!promoting) {
        out.add({uint8_t(from), uint8_t(to), 0, flag});
        return;
    }
    for (uint8_t p : {uint8_t(QUEEN), uint8_t(ROOK), uint8_t(BISHOP), uint8_t(KNIGHT)})
        out.add({uint8_t(from), uint8_t(to), p, F_QUIET});
}

void genPawn(const Board& b, Square from, Color us, MoveList& out) {
    const int fwd = (us == WHITE) ? 8 : -8;
    const int startRank = (us == WHITE) ? 1 : 6;
    const int promoRank = (us == WHITE) ? 6 : 1;
    const int r = rankOf(from), f = fileOf(from);
    const bool promoting = (r == promoRank);

    const Square one = from + fwd;
    if (b.sq[one] == EMPTY) {
        addPawnMove(out, from, one, promoting, F_QUIET);
        const Square two = one + fwd;
        if (r == startRank && b.sq[two] == EMPTY)
            out.add({uint8_t(from), uint8_t(two), 0, F_DOUBLE});
    }

    for (int df : {-1, 1}) {
        const int nf = f + df;
        if (nf < 0 || nf > 7) continue;
        const Square to = from + fwd + df;
        const PieceCode target = b.sq[to];
        if (target != EMPTY && colorOf(target) != us)
            addPawnMove(out, from, to, promoting, F_QUIET);
        else if (target == EMPTY && to == b.ep)
            out.add({uint8_t(from), uint8_t(to), 0, F_EP});
    }
}

void genStepper(const Board& b, Square from, Color us, MoveList& out, const int (*dirs)[2]) {
    const int f = fileOf(from), r = rankOf(from);
    for (int i = 0; i < 8; ++i) {
        const int nf = f + dirs[i][0], nr = r + dirs[i][1];
        if (!onBoard(nf, nr)) continue;
        const Square to = makeSq(nf, nr);
        const PieceCode target = b.sq[to];
        if (target == EMPTY || colorOf(target) != us)
            out.add({uint8_t(from), uint8_t(to), 0, F_QUIET});
    }
}

void genSlider(const Board& b, Square from, Color us, MoveList& out, const int (*dirs)[2], int ndirs) {
    const int f = fileOf(from), r = rankOf(from);
    for (int i = 0; i < ndirs; ++i) {
        for (int nf = f + dirs[i][0], nr = r + dirs[i][1]; onBoard(nf, nr); nf += dirs[i][0], nr += dirs[i][1]) {
            const Square to = makeSq(nf, nr);
            const PieceCode target = b.sq[to];
            if (target == EMPTY) {
                out.add({uint8_t(from), uint8_t(to), 0, F_QUIET});
                continue;
            }
            if (colorOf(target) != us) out.add({uint8_t(from), uint8_t(to), 0, F_QUIET});
            break;
        }
    }
}

// Castling is generated only when the king is not in check, the squares between
// king and rook are empty, and every square the king *travels through* is safe.
// The b1/b8 square may be attacked -- only the king's path matters.
void genCastling(const Board& b, Color us, MoveList& out) {
    const Color them = ~us;
    // The rook-presence test is not redundant with the rights mask: a
    // hand-written or hostile FEN can declare rights with no rook on the corner.
    if (us == WHITE) {
        const PieceCode king = encode(WHITE, KING), rook = encode(WHITE, ROOK);
        if (b.sq[4] != king) return;
        if (b.attacked(4, them)) return;
        if ((b.castling & WK) && b.sq[7] == rook && b.sq[5] == EMPTY && b.sq[6] == EMPTY && !b.attacked(5, them))
            out.add({4, 6, 0, F_CASTLE});
        if ((b.castling & WQ) && b.sq[0] == rook && b.sq[3] == EMPTY && b.sq[2] == EMPTY && b.sq[1] == EMPTY &&
            !b.attacked(3, them))
            out.add({4, 2, 0, F_CASTLE});
    } else {
        const PieceCode king = encode(BLACK, KING), rook = encode(BLACK, ROOK);
        if (b.sq[60] != king) return;
        if (b.attacked(60, them)) return;
        if ((b.castling & BK) && b.sq[63] == rook && b.sq[61] == EMPTY && b.sq[62] == EMPTY && !b.attacked(61, them))
            out.add({60, 62, 0, F_CASTLE});
        if ((b.castling & BQ) && b.sq[56] == rook && b.sq[59] == EMPTY && b.sq[58] == EMPTY && b.sq[57] == EMPTY &&
            !b.attacked(59, them))
            out.add({60, 58, 0, F_CASTLE});
    }
}

void genPseudo(const Board& b, MoveList& out) {
    const Color us = b.stm;
    for (Square s = 0; s < 64; ++s) {
        const PieceCode p = b.sq[s];
        if (p == EMPTY || colorOf(p) != us) continue;
        switch (typeOf(p)) {
            case PAWN:   genPawn(b, s, us, out); break;
            case KNIGHT: genStepper(b, s, us, out, KNIGHT_D); break;
            case KING:   genStepper(b, s, us, out, KING_D); break;
            case BISHOP: genSlider(b, s, us, out, BISHOP_D, 4); break;
            case ROOK:   genSlider(b, s, us, out, ROOK_D, 4); break;
            case QUEEN:
                genSlider(b, s, us, out, BISHOP_D, 4);
                genSlider(b, s, us, out, ROOK_D, 4);
                break;
        }
    }
    genCastling(b, us, out);
}

}  // namespace

void genLegal(Board& b, MoveList& out) {
    MoveList pseudo;
    genPseudo(b, pseudo);

    const Color us = b.stm;
    Undo u;
    for (const Move& m : pseudo) {
        b.makeMove(m, u);
        // Legality is decided after the fact: this catches absolute pins and the
        // en-passant discovered check, which no from/to test can see.
        if (!b.attacked(b.kingSq(us), ~us)) out.add(m);
        b.unmakeMove(m, u);
    }
}

}  // namespace gambit

#include "board.h"

#include <cctype>
#include <sstream>

namespace gambit {

namespace {

constexpr int KNIGHT_D[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KING_D[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
constexpr int BISHOP_D[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
constexpr int ROOK_D[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

char pieceChar(PieceCode p) {
    const char* w = " PNBRQK";
    char c = w[typeOf(p)];
    return colorOf(p) == WHITE ? c : char(std::tolower(c));
}

}  // namespace

void Board::setStart() {
    setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool Board::setFen(const std::string& fen) {
    for (int i = 0; i < 64; ++i) sq[i] = EMPTY;
    castling = 0;
    ep = NO_SQ;
    halfmove = 0;
    fullmove = 1;

    std::istringstream ss(fen);
    std::string placement, side, rights, epStr;
    if (!(ss >> placement >> side >> rights >> epStr)) return false;

    int f = 0, r = 7;  // FEN starts at rank 8 (index 56) and walks down
    for (char c : placement) {
        if (c == '/') {
            --r;
            f = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            f += c - '0';
        } else {
            Color col = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
            PieceType t;
            switch (std::tolower(static_cast<unsigned char>(c))) {
                case 'p': t = PAWN; break;
                case 'n': t = KNIGHT; break;
                case 'b': t = BISHOP; break;
                case 'r': t = ROOK; break;
                case 'q': t = QUEEN; break;
                case 'k': t = KING; break;
                default: return false;
            }
            if (!onBoard(f, r)) return false;
            sq[makeSq(f, r)] = encode(col, t);
            ++f;
        }
    }

    stm = (side == "w") ? WHITE : BLACK;

    for (char c : rights) {
        if (c == 'K') castling |= WK;
        else if (c == 'Q') castling |= WQ;
        else if (c == 'k') castling |= BK;
        else if (c == 'q') castling |= BQ;
    }

    if (epStr != "-" && epStr.size() == 2) ep = makeSq(epStr[0] - 'a', epStr[1] - '1');

    int hm = 0, fm = 1;
    if (ss >> hm) halfmove = uint16_t(hm);
    if (ss >> fm) fullmove = uint16_t(fm);
    return true;
}

std::string Board::fen() const {
    std::ostringstream out;
    for (int r = 7; r >= 0; --r) {
        int run = 0;
        for (int f = 0; f < 8; ++f) {
            PieceCode p = sq[makeSq(f, r)];
            if (p == EMPTY) {
                ++run;
            } else {
                if (run) out << run, run = 0;
                out << pieceChar(p);
            }
        }
        if (run) out << run;
        if (r) out << '/';
    }
    out << (stm == WHITE ? " w " : " b ");

    if (!castling) {
        out << '-';
    } else {
        if (castling & WK) out << 'K';
        if (castling & WQ) out << 'Q';
        if (castling & BK) out << 'k';
        if (castling & BQ) out << 'q';
    }

    if (ep == NO_SQ) out << " -";
    else out << ' ' << char('a' + fileOf(ep)) << char('1' + rankOf(ep));

    out << ' ' << halfmove << ' ' << fullmove;
    return out.str();
}

Square Board::kingSq(Color c) const {
    PieceCode k = encode(c, KING);
    for (int i = 0; i < 64; ++i)
        if (sq[i] == k) return i;
    return NO_SQ;
}

bool Board::attacked(Square s, Color by) const {
    const int f = fileOf(s), r = rankOf(s);

    // Pawns. A white pawn on (f±1, r-1) attacks (f, r).
    const int pr = (by == WHITE) ? r - 1 : r + 1;
    if (pr >= 0 && pr < 8) {
        const PieceCode pawn = encode(by, PAWN);
        if (f - 1 >= 0 && sq[makeSq(f - 1, pr)] == pawn) return true;
        if (f + 1 < 8 && sq[makeSq(f + 1, pr)] == pawn) return true;
    }

    const PieceCode knight = encode(by, KNIGHT);
    for (auto& d : KNIGHT_D) {
        int nf = f + d[0], nr = r + d[1];
        if (onBoard(nf, nr) && sq[makeSq(nf, nr)] == knight) return true;
    }

    const PieceCode king = encode(by, KING);
    for (auto& d : KING_D) {
        int nf = f + d[0], nr = r + d[1];
        if (onBoard(nf, nr) && sq[makeSq(nf, nr)] == king) return true;
    }

    const PieceCode bishop = encode(by, BISHOP), queen = encode(by, QUEEN);
    for (auto& d : BISHOP_D) {
        for (int nf = f + d[0], nr = r + d[1]; onBoard(nf, nr); nf += d[0], nr += d[1]) {
            PieceCode p = sq[makeSq(nf, nr)];
            if (p == EMPTY) continue;
            if (p == bishop || p == queen) return true;
            break;
        }
    }

    const PieceCode rook = encode(by, ROOK);
    for (auto& d : ROOK_D) {
        for (int nf = f + d[0], nr = r + d[1]; onBoard(nf, nr); nf += d[0], nr += d[1]) {
            PieceCode p = sq[makeSq(nf, nr)];
            if (p == EMPTY) continue;
            if (p == rook || p == queen) return true;
            break;
        }
    }

    return false;
}

void Board::makeMove(const Move& m, Undo& u) {
    const Color us = stm;
    const PieceCode pc = sq[m.from];
    const PieceType t = typeOf(pc);
    const int fwd = (us == WHITE) ? 8 : -8;

    u.captured = EMPTY;
    u.capturedSq = NO_SQ;
    u.castling = castling;
    u.ep = ep;
    u.halfmove = halfmove;

    if (m.flag == F_EP) {
        // The captured pawn sits behind the landing square, not on it.
        Square cs = m.to - fwd;
        u.captured = sq[cs];
        u.capturedSq = cs;
        sq[cs] = EMPTY;
    } else if (sq[m.to] != EMPTY) {
        u.captured = sq[m.to];
        u.capturedSq = m.to;
    }

    sq[m.to] = pc;
    sq[m.from] = EMPTY;
    if (m.promo) sq[m.to] = encode(us, PieceType(m.promo));

    if (m.flag == F_CASTLE) {
        switch (m.to) {
            case 6:  sq[5] = sq[7];   sq[7] = EMPTY;  break;  // e1g1
            case 2:  sq[3] = sq[0];   sq[0] = EMPTY;  break;  // e1c1
            case 62: sq[61] = sq[63]; sq[63] = EMPTY; break;  // e8g8
            case 58: sq[59] = sq[56]; sq[56] = EMPTY; break;  // e8c8
        }
    }

    // A king move forfeits both rights; a rook leaving or being captured on its
    // home square forfeits that side. Testing both `from` and `to` covers both.
    if (t == KING) castling &= (us == WHITE) ? uint8_t(~(WK | WQ)) : uint8_t(~(BK | BQ));
    if (m.from == 0 || m.to == 0) castling &= uint8_t(~WQ);
    if (m.from == 7 || m.to == 7) castling &= uint8_t(~WK);
    if (m.from == 56 || m.to == 56) castling &= uint8_t(~BQ);
    if (m.from == 63 || m.to == 63) castling &= uint8_t(~BK);

    ep = (m.flag == F_DOUBLE) ? Square(m.from + fwd) : NO_SQ;
    halfmove = (t == PAWN || u.captured != EMPTY) ? 0 : uint16_t(halfmove + 1);
    if (us == BLACK) ++fullmove;
    stm = ~us;
}

void Board::unmakeMove(const Move& m, const Undo& u) {
    stm = ~stm;
    const Color us = stm;

    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    if (us == BLACK) --fullmove;

    if (m.flag == F_CASTLE) {
        switch (m.to) {
            case 6:  sq[7] = sq[5];   sq[5] = EMPTY;  break;
            case 2:  sq[0] = sq[3];   sq[3] = EMPTY;  break;
            case 62: sq[63] = sq[61]; sq[61] = EMPTY; break;
            case 58: sq[56] = sq[59]; sq[59] = EMPTY; break;
        }
    }

    PieceCode pc = sq[m.to];
    if (m.promo) pc = encode(us, PAWN);
    sq[m.from] = pc;
    sq[m.to] = EMPTY;

    if (u.captured != EMPTY) sq[u.capturedSq] = u.captured;
}

std::string toUci(const Move& m) {
    std::string s;
    s += char('a' + fileOf(m.from));
    s += char('1' + rankOf(m.from));
    s += char('a' + fileOf(m.to));
    s += char('1' + rankOf(m.to));
    if (m.promo) s += " nbrq"[m.promo - 1];
    return s;
}

}  // namespace gambit

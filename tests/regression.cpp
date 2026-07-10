// Regression tests. Each case is a defect that existed in chess-engine v1 and
// was confirmed by running its logic against a hand-built position. perft proves
// the aggregate node counts are right; these prove the specific bugs are dead
// and name them, so a future refactor that resurrects one fails loudly.

#include <iostream>
#include <string>
#include <vector>

#include "../engine/board.h"
#include "../engine/movegen.h"

using namespace gambit;

static int failures = 0;

static void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::cout << (ok ? "  ok   " : "  FAIL ") << name;
    if (!ok && !detail.empty()) std::cout << "  -- " << detail;
    std::cout << "\n";
    if (!ok) ++failures;
}

static std::vector<std::string> legalUci(const std::string& fen) {
    Board b;
    if (!b.setFen(fen)) return {"<bad fen>"};
    MoveList ml;
    genLegal(b, ml);
    std::vector<std::string> out;
    for (const Move& m : ml) out.push_back(toUci(m));
    return out;
}

static bool has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

static int countFrom(const std::vector<std::string>& v, const std::string& sq) {
    int n = 0;
    for (const auto& x : v) if (x.substr(0, 2) == sq) ++n;
    return n;
}

int main() {
    std::cout << "regression: defects carried over from chess-engine v1\n\n";

    // v1 bug A: isStrictlyLegal() read the piece off an already-emptied square,
    // so it validated the wrong king. Pinned pieces moved freely.
    {
        auto mv = legalUci("4r2k/8/8/8/8/8/4N3/4K3 w - - 0 1");
        check(countFrom(mv, "e2") == 0, "absolute pin: pinned knight has no moves",
              "got " + std::to_string(countFrom(mv, "e2")));
    }

    // v1 bug C: giving check froze every white piece except the checking one.
    // Here white is in check from the rook and has exactly three replies:
    // Kd1, Kf1 (Kd2/Kf2 stay on the e-file... no: they leave it) -- enumerate.
    {
        auto mv = legalUci("4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
        // King on e1, black rook e8. Legal: Kd1, Kf1, Kd2, Kf2.
        check(mv.size() == 4, "in check: king has exactly 4 escape squares",
              "got " + std::to_string(mv.size()));
        check(!has(mv, "e1e2"), "in check: king may not stay on the pinned file");
    }

    // v1 bug E: the castle branch was gated on `start == 4`, which is e8.
    // The white king (e1 == 60 in v1's indexing) could never castle at all.
    {
        auto mv = legalUci("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        check(has(mv, "e1g1"), "white may castle kingside");
        check(has(mv, "e1c1"), "white may castle queenside");
    }

    // v1 bug D: black's king "castled" by teleporting two squares -- the rook
    // never moved, because the rook-execution code only ran for W_KING.
    {
        Board b;
        b.setFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
        auto mv = legalUci(b.fen());
        check(has(mv, "e8g8"), "black may castle kingside");

        Move castle{60, 62, 0, F_CASTLE};
        Undo u;
        b.makeMove(castle, u);
        check(b.sq[62] == encode(BLACK, KING), "castling: black king lands on g8");
        check(b.sq[61] == encode(BLACK, ROOK), "castling: h8 rook relocates to f8");
        check(b.sq[63] == EMPTY, "castling: h8 is vacated");
        b.unmakeMove(castle, u);
        check(b.sq[60] == encode(BLACK, KING) && b.sq[63] == encode(BLACK, ROOK),
              "castling: unmake restores king and rook");
    }

    // v1 never checked that the king passes through safe squares.
    // Black rook on f8 rakes f1, so O-O is illegal; O-O-O remains legal.
    {
        auto mv = legalUci("5r1k/8/8/8/8/8/8/R3K2R w KQ - 0 1");
        check(!has(mv, "e1g1"), "castling: may not pass through an attacked square");
        check(has(mv, "e1c1"), "castling: queenside still legal (b1 attack is irrelevant)");
    }

    // b1 may be attacked during queenside castling -- only the king's path
    // (e1, d1, c1) must be safe. A generator that also guards b1 is too strict.
    {
        auto mv = legalUci("1r5k/8/8/8/8/8/8/R3K3 w Q - 0 1");
        check(has(mv, "e1c1"), "castling: queenside legal even when b1 is attacked");
    }

    // v1 had no promotion at all: a pawn reaching the last rank stayed a pawn.
    {
        auto mv = legalUci("7k/P7/8/8/8/8/8/K7 w - - 0 1");
        check(countFrom(mv, "a7") == 4, "promotion: pawn push yields exactly 4 promotions",
              "got " + std::to_string(countFrom(mv, "a7")));
        check(has(mv, "a7a8q") && has(mv, "a7a8n"), "promotion: queen and knight both offered");
    }

    // v1 had no en passant. This is the discovered-check case: dxe3 e.p. removes
    // BOTH pawns from rank 4, exposing the black king on a4 to the queen on h4.
    // Only a make/unmake legality test can see this.
    {
        auto mv = legalUci("8/8/8/8/k2pP2Q/8/8/3K4 b - e3 0 1");
        check(!has(mv, "d4e3"), "en passant: illegal when it uncovers a discovered check");
    }

    // And the same shape without the queen -- now it must be legal.
    {
        auto mv = legalUci("8/8/8/8/k2pP3/8/8/3K4 b - e3 0 1");
        check(has(mv, "d4e3"), "en passant: legal when no discovered check exists");
    }

    // Castling rights must die when the rook is *captured* on its home square,
    // not only when it moves. v1 only ever set the flag on a drag.
    {
        Board b;
        b.setFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        Move rxr{0, 56, 0, F_QUIET};  // Ra1xa8
        Undo u;
        b.makeMove(rxr, u);
        check(!(b.castling & BQ), "rights: capturing a8 rook clears black queenside");
        check(!(b.castling & WQ), "rights: moving a1 rook clears white queenside");
        b.unmakeMove(rxr, u);
        check(b.castling == (WK | WQ | BK | BQ), "rights: unmake restores all four");
    }

    // A hostile FEN can claim rights with no rook present. Do not emit a castle
    // that would move an empty square onto f1.
    {
        auto mv = legalUci("4k3/8/8/8/8/8/8/4K3 w KQ - 0 1");
        check(!has(mv, "e1g1") && !has(mv, "e1c1"), "castling: rights without a rook generate nothing");
    }

    std::cout << "\n" << (failures ? "FAILED" : "PASSED") << " -- " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}

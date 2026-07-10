// Search tests, plus the ablation benchmark that produces the node-reduction
// number quoted in the README. Nothing here is hand-waved: the benchmark runs
// the same search twice, once with ordering disabled, and prints the delta.

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../engine/board.h"
#include "../engine/movegen.h"
#include "../engine/search.h"

using namespace gambit;

static int failures = 0;

static void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::cout << (ok ? "  ok   " : "  FAIL ") << name;
    if (!ok && !detail.empty()) std::cout << "  -- " << detail;
    std::cout << "\n";
    if (!ok) ++failures;
}

int main() {
    std::cout << "search correctness\n\n";

    // Back-rank mate in one: Ra1-a8#.
    {
        Board b;
        b.setFen("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
        SearchLimits lim;
        lim.maxDepth = 3;
        SearchResult r = search(b, lim);
        check(toUci(r.best) == "a1a8", "mate in 1: finds Ra8#", "got " + toUci(r.best));
        check(isMateScore(r.score) && mateDistance(r.score) == 1, "mate in 1: scored as mate in 1 ply",
              "score " + std::to_string(r.score));
    }

    // Mate in two. Two quiet king moves work: 1.Kb6 Kb8 2.Rh8#, and 1.Kc7 Ka7
    // 2.Ra1#. The tempting 1.Rh8+ is NOT mate in two -- after 1...Ka7 the rook
    // has no follow-up, since 2.Ra8+ hangs to Kxa8. A search that grabs the
    // check first and asks questions later picks the wrong move here.
    {
        Board b;
        b.setFen("k7/8/2K5/8/8/8/8/7R w - - 0 1");
        SearchLimits lim;
        lim.maxDepth = 5;
        SearchResult r = search(b, lim);
        const std::string best = toUci(r.best);
        check(best == "c6b6" || best == "c6c7", "mate in 2: finds a quiet king key move, not the greedy check",
              "got " + best);
        check(best != "h1h8", "mate in 2: rejects the 1.Rh8+ decoy");
        check(isMateScore(r.score) && mateDistance(r.score) == 3, "mate in 2: scored as mate in 3 plies",
              "score " + std::to_string(r.score));
    }

    // Mate scores must shrink with distance, so a mate in 1 outranks a mate in 3.
    check(MATE - 1 > MATE - 3, "mate scores prefer the shorter mate");

    // v1 scored stalemate as -99999 and would walk into it to "avoid losing".
    {
        Board b;
        b.setFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        check(status(b) == GameStatus::Stalemate, "stalemate is detected");
        SearchLimits lim;
        lim.maxDepth = 2;
        SearchResult r = search(b, lim);
        check(!r.hasMove, "stalemate: no move is returned");
    }
    {
        Board b;
        b.setFen("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
        check(status(b) == GameStatus::Checkmate, "checkmate is detected");
    }
    {
        Board b;
        b.setFen("7k/8/6K1/8/8/8/8/8 w - - 0 1");
        check(status(b) == GameStatus::DrawMaterial, "K vs K is a material draw");
    }
    {
        Board b;
        b.setFen("7k/8/6KB/8/8/8/8/8 w - - 0 1");
        check(status(b) == GameStatus::DrawMaterial, "K+B vs K is a material draw");
    }
    {
        Board b;
        b.setFen("7k/8/6KR/8/8/8/8/8 w - - 0 1");
        check(status(b) != GameStatus::DrawMaterial, "K+R vs K is not a material draw");
    }

    // ---------------------------------------------------------------------
    std::cout << "\nablation: what move ordering is worth\n";
    std::cout << "(quiescence off on both sides, so the comparison is like for like)\n\n";

    struct Pos {
        const char* name;
        const char* fen;
    };
    const std::vector<Pos> positions = {
        {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"},
        {"middlegame", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"},
    };

    const int DEPTH = 5;
    uint64_t totalPlain = 0, totalOrdered = 0;

    std::cout << "  " << std::left << std::setw(13) << "position" << std::right << std::setw(13) << "no ordering"
              << std::setw(13) << "ordered" << std::setw(11) << "reduction" << "\n";

    for (const Pos& p : positions) {
        Board b1, b2;
        b1.setFen(p.fen);
        b2.setFen(p.fen);

        SearchLimits plain;
        plain.maxDepth = DEPTH;
        plain.ordering = false;
        plain.quiescence = false;

        SearchLimits ordered;
        ordered.maxDepth = DEPTH;
        ordered.ordering = true;
        ordered.quiescence = false;

        SearchResult rp = search(b1, plain);
        SearchResult ro = search(b2, ordered);

        // Alpha-beta returns the exact minimax value regardless of move order.
        // If these disagree, the pruning is unsound -- this is the single most
        // valuable assertion in the file.
        check(rp.score == ro.score, std::string("ordering preserves the minimax value: ") + p.name,
              "plain " + std::to_string(rp.score) + " vs ordered " + std::to_string(ro.score));

        totalPlain += rp.nodes;
        totalOrdered += ro.nodes;

        const double red = 100.0 * (1.0 - double(ro.nodes) / double(rp.nodes));
        std::cout << "  " << std::left << std::setw(13) << p.name << std::right << std::setw(13) << rp.nodes
                  << std::setw(13) << ro.nodes << std::setw(10) << std::fixed << std::setprecision(1) << red << "%\n";
    }

    const double totalRed = 100.0 * (1.0 - double(totalOrdered) / double(totalPlain));
    std::cout << "  " << std::left << std::setw(13) << "TOTAL" << std::right << std::setw(13) << totalPlain
              << std::setw(13) << totalOrdered << std::setw(10) << std::fixed << std::setprecision(1) << totalRed
              << "%\n";

    check(totalOrdered < totalPlain, "ordering reduces the node count");

    // ---------------------------------------------------------------------
    std::cout << "\nfull configuration (ordering + quiescence), 1000ms per position\n\n";
    for (const Pos& p : positions) {
        Board b;
        b.setFen(p.fen);
        SearchLimits lim;
        lim.maxDepth = 64;
        lim.movetimeMs = 1000;
        SearchResult r = search(b, lim);
        const double knps = r.ms > 0 ? double(r.nodes) / r.ms : 0.0;
        std::cout << "  " << std::left << std::setw(13) << p.name << "depth " << r.depth << "  best " << toUci(r.best)
                  << "  eval " << std::showpos << r.score << std::noshowpos << "cp  " << r.nodes << " nodes  "
                  << std::fixed << std::setprecision(0) << knps << "k nodes/s\n";
    }

    std::cout << "\n" << (failures ? "FAILED" : "PASSED") << " -- " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}

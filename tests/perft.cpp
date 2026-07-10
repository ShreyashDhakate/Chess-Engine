// perft -- the standard move-generator verification harness.
//
// perft(d) counts the leaf nodes of the legal-move tree at depth d. The counts
// below are the published reference values from the Chess Programming Wiki. A
// generator that reproduces all of them has almost certainly got pins,
// en-passant (including the discovered-check case), castling rights, castling
// through check, and underpromotion right -- because each position was chosen
// to break a generator that does not.
//
//   ./perft          fast suite  (~26M nodes)
//   ./perft --deep   adds depth-6 start position and Kiwipete depth 5 (~400M)
//   ./perft --divide "<fen>" <depth>   per-move breakdown, for bisecting a bug

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../engine/board.h"
#include "../engine/movegen.h"

using namespace gambit;

static uint64_t perft(Board& b, int depth) {
    MoveList ml;
    genLegal(b, ml);
    if (depth <= 1) return ml.size;

    uint64_t nodes = 0;
    Undo u;
    for (const Move& m : ml) {
        b.makeMove(m, u);
        nodes += perft(b, depth - 1);
        b.unmakeMove(m, u);
    }
    return nodes;
}

static void divide(Board& b, int depth) {
    MoveList ml;
    genLegal(b, ml);
    uint64_t total = 0;
    Undo u;
    for (const Move& m : ml) {
        b.makeMove(m, u);
        uint64_t n = (depth <= 1) ? 1 : perft(b, depth - 1);
        b.unmakeMove(m, u);
        std::cout << toUci(m) << ": " << n << "\n";
        total += n;
    }
    std::cout << "\nnodes: " << total << "  moves: " << ml.size << "\n";
}

struct Case {
    const char* name;
    const char* fen;
    std::vector<uint64_t> expect;  // index 0 => depth 1
};

// Reference values: chessprogramming.org/Perft_Results
static const std::vector<Case> FAST = {
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     {20, 400, 8902, 197281, 4865609}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     {48, 2039, 97862, 4085603}},
    {"position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
     {14, 191, 2812, 43238, 674624, 11030083}},
    {"position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
     {6, 264, 9467, 422333}},
    {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
     {44, 1486, 62379, 2103487}},
    {"position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
     {46, 2079, 89890, 3894594}},
};

static const std::vector<Case> DEEP = {
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
     {20, 400, 8902, 197281, 4865609, 119060324}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     {48, 2039, 97862, 4085603, 193690690}},
    {"position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
     {44, 1486, 62379, 2103487, 89941194}},
};

int main(int argc, char** argv) {
    if (argc >= 4 && std::strcmp(argv[1], "--divide") == 0) {
        Board b;
        if (!b.setFen(argv[2])) {
            std::cerr << "bad fen\n";
            return 2;
        }
        divide(b, std::stoi(argv[3]));
        return 0;
    }

    const bool deep = (argc >= 2 && std::strcmp(argv[1], "--deep") == 0);
    const auto& suite = deep ? DEEP : FAST;

    int failures = 0;
    uint64_t grandTotal = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (const Case& c : suite) {
        Board b;
        if (!b.setFen(c.fen)) {
            std::cerr << "bad fen: " << c.name << "\n";
            return 2;
        }
        // FEN round-trip: catches parser/serializer drift for free.
        if (b.fen() != c.fen) {
            std::cout << "  [warn] fen round-trip differs for " << c.name << "\n"
                      << "         in:  " << c.fen << "\n         out: " << b.fen() << "\n";
        }

        for (size_t i = 0; i < c.expect.size(); ++i) {
            const int depth = int(i) + 1;
            const uint64_t want = c.expect[i];

            const auto s = std::chrono::steady_clock::now();
            const uint64_t got = perft(b, depth);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - s).count();
            grandTotal += got;

            const bool ok = (got == want);
            if (!ok) ++failures;

            std::cout << (ok ? "  ok   " : "  FAIL ") << std::left << std::setw(11) << c.name
                      << " d" << depth << "  " << std::right << std::setw(11) << got;
            if (!ok) std::cout << "   expected " << want;
            else if (secs > 0.05)
                std::cout << "   " << std::fixed << std::setprecision(2) << (got / secs / 1e6) << " Mnps";
            std::cout << "\n";
        }
    }

    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "\n" << (failures ? "FAILED " : "PASSED ") << grandTotal << " nodes in "
              << std::fixed << std::setprecision(2) << total << "s";
    if (total > 0) std::cout << "  (" << (grandTotal / total / 1e6) << " Mnps)";
    std::cout << "\n";
    if (failures) std::cout << failures << " check(s) failed\n";
    return failures ? 1 : 0;
}

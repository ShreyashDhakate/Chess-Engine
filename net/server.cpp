// gambitd -- a single-threaded, edge-triggered epoll WebSocket chess server.
//
// One process, one thread, no locks. Every socket is non-blocking; every read
// drains to EAGAIN because edge-triggered epoll only reports a transition, not
// a level. Partial writes are buffered and EPOLLOUT is armed until they land.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/board.h"
#include "../engine/eval.h"
#include "../engine/movegen.h"
#include "../engine/search.h"
#include "http.h"
#include "ws.h"

using namespace gambit;
using namespace gambit::net;

namespace {

constexpr size_t MAX_IN = 1 << 20;   // 1 MiB of unparsed input, then hang up
constexpr size_t MAX_OUT = 8 << 20;  // 8 MiB of unsent output, then hang up

struct Options {
    std::string bind = "127.0.0.1";  // loopback by default: the proxy terminates TLS
    int port = 8080;
    int movetimeMs = 500;
    std::string publicDir = "public";
};

// ---------------------------------------------------------------- static files

struct Asset {
    std::string contentType;
    std::string body;
};

// Loaded once at startup and served from memory. Because the map is a fixed
// whitelist built here, no request path can ever reach the filesystem, so
// directory traversal ("GET /../../etc/passwd") is impossible by construction.
std::map<std::string, Asset> loadAssets(const std::string& dir) {
    const std::vector<std::pair<std::string, std::string>> files = {
        {"/", "index.html"},
        {"/index.html", "index.html"},
        {"/app.js", "app.js"},
        {"/style.css", "style.css"},
    };
    const std::map<std::string, std::string> types = {
        {"html", "text/html; charset=utf-8"},
        {"js", "application/javascript; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},
    };

    std::map<std::string, Asset> out;
    for (const auto& [route, name] : files) {
        std::ifstream f(dir + "/" + name, std::ios::binary);
        if (!f) {
            std::cerr << "warning: missing asset " << dir << "/" << name << "\n";
            continue;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string ext = name.substr(name.rfind('.') + 1);
        out[route] = Asset{types.count(ext) ? types.at(ext) : "application/octet-stream", ss.str()};
    }
    return out;
}

// ---------------------------------------------------------------- tiny JSON

std::string jsonGetString(const std::string& s, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return "";
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    p = s.find('"', p);
    if (p == std::string::npos) return "";
    const size_t end = s.find('"', p + 1);
    if (end == std::string::npos) return "";
    return s.substr(p + 1, end - p - 1);
}

long jsonGetInt(const std::string& s, const std::string& key, long def) {
    const std::string pat = "\"" + key + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return def;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return def;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    try {
        return std::stol(s.substr(p));
    } catch (...) {
        return def;
    }
}

// ---------------------------------------------------------------- game session

struct Session {
    Board board;
    std::vector<std::string> seen;  // position keys, for threefold repetition
    std::string lastMove;

    void reset() {
        board.setStart();
        seen.clear();
        lastMove.clear();
        remember();
    }
    // A position for repetition purposes is placement + side + castling + ep.
    // The halfmove and fullmove counters must not participate.
    void remember() {
        const std::string f = board.fen();
        size_t sp = 0;
        int fields = 0;
        for (size_t i = 0; i < f.size(); ++i) {
            if (f[i] == ' ' && ++fields == 4) {
                sp = i;
                break;
            }
        }
        seen.push_back(f.substr(0, sp));
    }
    bool threefold() const {
        if (seen.empty()) return false;
        int n = 0;
        for (const auto& k : seen)
            if (k == seen.back()) ++n;
        return n >= 3;
    }
};

const char* statusName(GameStatus s) {
    switch (s) {
        case GameStatus::Ongoing: return "ongoing";
        case GameStatus::Check: return "check";
        case GameStatus::Checkmate: return "checkmate";
        case GameStatus::Stalemate: return "stalemate";
        case GameStatus::DrawFiftyMove: return "draw_fifty";
        case GameStatus::DrawMaterial: return "draw_material";
    }
    return "ongoing";
}

std::string stateMessage(Session& s, int evalCp, int depth, uint64_t nodes, int searchMs) {
    GameStatus st = status(s.board);
    if (st == GameStatus::Ongoing && s.threefold()) st = GameStatus::DrawFiftyMove;

    MoveList ml;
    genLegal(s.board, ml);

    std::ostringstream o;
    o << "{\"t\":\"state\",\"fen\":\"" << s.board.fen() << "\",\"turn\":\""
      << (s.board.stm == WHITE ? "w" : "b") << "\",\"status\":\"" << statusName(st) << "\",\"last\":\""
      << s.lastMove << "\",\"eval\":" << evalCp << ",\"depth\":" << depth << ",\"nodes\":" << nodes
      << ",\"searchMs\":" << searchMs << ",\"legal\":[";
    for (int i = 0; i < ml.size; ++i) {
        if (i) o << ',';
        o << '"' << toUci(ml.moves[i]) << '"';
    }
    o << "]}";
    return o.str();
}

// ---------------------------------------------------------------- connections

struct Conn {
    int fd = -1;
    bool upgraded = false;
    bool closing = false;
    std::string in, out;
    bool wantWrite = false;

    std::string fragment;
    Opcode fragOp = Opcode::Text;
    bool fragmenting = false;

    Session game;
};

int epfd = -1;

void arm(Conn& c, bool write) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | (write ? uint32_t(EPOLLOUT) : 0u);
    ev.data.fd = c.fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &ev);
    c.wantWrite = write;
}

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags != -1 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Returns false if the connection must be dropped.
bool flushOut(Conn& c) {
    while (!c.out.empty()) {
        const ssize_t n = ::write(c.fd, c.out.data(), c.out.size());
        if (n > 0) {
            c.out.erase(0, size_t(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Kernel buffer is full. Keep the tail and wait for EPOLLOUT rather
            // than spinning or blocking the whole event loop on one slow peer.
            if (!c.wantWrite) arm(c, true);
            return true;
        }
        return false;
    }
    if (c.wantWrite) arm(c, false);
    return !c.closing;
}

void send(Conn& c, const std::string& data) {
    if (c.out.size() + data.size() > MAX_OUT) {
        c.closing = true;
        return;
    }
    c.out += data;
}

void sendText(Conn& c, const std::string& payload) { send(c, buildFrame(Opcode::Text, payload)); }

// ---------------------------------------------------------------- protocol

void handleMessage(Conn& c, const std::string& msg, const Options& opt) {
    const std::string t = jsonGetString(msg, "t");

    if (t == "ping") {
        // Answered before any work, so bench.py measures the network path and
        // the event loop, not the chess search.
        std::ostringstream o;
        o << "{\"t\":\"pong\",\"id\":" << jsonGetInt(msg, "id", 0) << "}";
        sendText(c, o.str());
        return;
    }

    if (t == "new") {
        c.game.reset();
        sendText(c, stateMessage(c.game, 0, 0, 0, 0));
        return;
    }

    if (t != "move") return;

    const std::string uci = jsonGetString(msg, "uci");
    MoveList ml;
    genLegal(c.game.board, ml);

    const Move* chosen = nullptr;
    for (const Move& m : ml)
        if (toUci(m) == uci) chosen = &m;

    if (!chosen) {
        sendText(c, "{\"t\":\"error\",\"msg\":\"illegal move\"}");
        return;
    }

    Undo u;
    c.game.board.makeMove(*chosen, u);
    c.game.lastMove = uci;
    c.game.remember();

    GameStatus st = status(c.game.board);
    if (st == GameStatus::Checkmate || st == GameStatus::Stalemate || st == GameStatus::DrawFiftyMove ||
        st == GameStatus::DrawMaterial || c.game.threefold()) {
        sendText(c, stateMessage(c.game, 0, 0, 0, 0));
        return;
    }

    SearchLimits lim;
    lim.maxDepth = 64;
    lim.movetimeMs = opt.movetimeMs;
    SearchResult r = search(c.game.board, lim);

    if (r.hasMove) {
        Undo u2;
        c.game.board.makeMove(r.best, u2);
        c.game.lastMove = toUci(r.best);
        c.game.remember();
    }

    // Search score is from the mover's point of view. The UI wants it from
    // White's, which is the convention every chess GUI uses.
    const int whiteCp = (c.game.board.stm == WHITE) ? r.score : -r.score;
    sendText(c, stateMessage(c.game, whiteCp, r.depth, r.nodes, r.ms));
}

// Returns false to drop the connection.
bool processWs(Conn& c, const Options& opt) {
    for (;;) {
        Frame f;
        size_t used = 0;
        const Parse p = parseFrame(c.in.data(), c.in.size(), used, f);
        if (p == Parse::NeedMore) return true;
        if (p == Parse::ProtocolError) return false;
        c.in.erase(0, used);

        switch (f.op) {
            case Opcode::Ping:
                send(c, buildFrame(Opcode::Pong, f.payload));
                break;
            case Opcode::Pong:
                break;
            case Opcode::Close:
                send(c, buildFrame(Opcode::Close, f.payload));
                c.closing = true;
                return true;
            case Opcode::Text:
            case Opcode::Binary:
                if (c.fragmenting) return false;  // a new data frame mid-fragment is illegal
                if (f.fin) {
                    handleMessage(c, f.payload, opt);
                } else {
                    c.fragmenting = true;
                    c.fragOp = f.op;
                    c.fragment = f.payload;
                }
                break;
            case Opcode::Continuation:
                if (!c.fragmenting) return false;
                if (c.fragment.size() + f.payload.size() > MAX_PAYLOAD) return false;
                c.fragment += f.payload;
                if (f.fin) {
                    c.fragmenting = false;
                    handleMessage(c, c.fragment, opt);
                    c.fragment.clear();
                }
                break;
        }
    }
}

bool processHttp(Conn& c, const std::map<std::string, Asset>& assets, const Options& opt) {
    HttpRequest req;
    size_t used = 0;
    const Parse p = parseRequest(c.in.data(), c.in.size(), used, req);
    if (p == Parse::NeedMore) return true;
    if (p == Parse::ProtocolError) return false;
    c.in.erase(0, used);

    if (isWebSocketUpgrade(req)) {
        send(c, handshakeResponse(req.get("Sec-WebSocket-Key")));
        c.upgraded = true;
        c.game.reset();
        sendText(c, stateMessage(c.game, 0, 0, 0, 0));
        return processWs(c, opt);
    }

    auto it = assets.find(req.path);
    if (it == assets.end()) {
        send(c, httpResponse("404 Not Found", "text/plain", "not found\n"));
    } else {
        send(c, httpResponse("200 OK", it->second.contentType, it->second.body));
    }
    c.closing = true;  // plain HTTP replies are Connection: close
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--port") opt.port = std::stoi(next());
        else if (a == "--bind") opt.bind = next();
        else if (a == "--movetime") opt.movetimeMs = std::stoi(next());
        else if (a == "--public") opt.publicDir = next();
        else if (a == "--help") {
            std::cout << "gambitd [--bind ADDR] [--port N] [--movetime MS] [--public DIR]\n";
            return 0;
        }
    }

    // A dead peer would otherwise kill the whole process on write().
    signal(SIGPIPE, SIG_IGN);

    const auto assets = loadAssets(opt.publicDir);

    const int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    // Without SO_REUSEADDR the listener cannot rebind while old connections sit
    // in TIME_WAIT -- a restart would fail for up to two minutes.
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(opt.port));
    if (inet_pton(AF_INET, opt.bind.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "bad bind address: " << opt.bind << "\n";
        return 1;
    }
    if (bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(lfd, 128) < 0) {
        perror("listen");
        return 1;
    }
    setNonBlocking(lfd);

    epfd = epoll_create1(0);
    epoll_event lev{};
    lev.events = EPOLLIN | EPOLLET;
    lev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &lev);

    std::unordered_map<int, Conn> conns;

    auto drop = [&](int fd) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        conns.erase(fd);
    };

    std::cout << "gambitd listening on " << opt.bind << ":" << opt.port << "  (movetime " << opt.movetimeMs
              << "ms)\n"
              << std::flush;

    std::vector<epoll_event> events(256);
    for (;;) {
        const int n = epoll_wait(epfd, events.data(), int(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            const uint32_t ev = events[i].events;

            if (fd == lfd) {
                // Edge-triggered: accept until EAGAIN or we lose the backlog.
                for (;;) {
                    const int cfd = accept(lfd, nullptr, nullptr);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        break;
                    }
                    setNonBlocking(cfd);
                    // Nagle batches small writes for up to 40ms waiting for an
                    // ACK. A move reply is ~1KB and latency-critical, so off.
                    int nodelay = 1;
                    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    cev.data.fd = cfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                    conns[cfd].fd = cfd;
                }
                continue;
            }

            auto it = conns.find(fd);
            if (it == conns.end()) continue;
            Conn& c = it->second;

            if (ev & (EPOLLERR | EPOLLHUP)) {
                drop(fd);
                continue;
            }

            if (ev & EPOLLOUT) {
                if (!flushOut(c)) {
                    drop(fd);
                    continue;
                }
                if (c.closing && c.out.empty()) {
                    drop(fd);
                    continue;
                }
            }

            if (ev & (EPOLLIN | EPOLLRDHUP)) {
                bool dead = false;
                for (;;) {
                    char buf[65536];
                    const ssize_t r = ::read(fd, buf, sizeof(buf));
                    if (r > 0) {
                        if (c.in.size() + size_t(r) > MAX_IN) {
                            dead = true;
                            break;
                        }
                        c.in.append(buf, size_t(r));
                        continue;
                    }
                    if (r == 0) {  // orderly half-close from the peer
                        dead = true;
                        break;
                    }
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    dead = true;
                    break;
                }
                if (dead) {
                    drop(fd);
                    continue;
                }

                const bool ok = c.upgraded ? processWs(c, opt) : processHttp(c, assets, opt);
                if (!ok) {
                    drop(fd);
                    continue;
                }
                if (!flushOut(c)) {
                    drop(fd);
                    continue;
                }
                if (c.closing && c.out.empty()) {
                    drop(fd);
                    continue;
                }
            }
        }
    }

    ::close(lfd);
    return 0;
}

# gambitd

A chess engine and WebSocket server written from scratch in C++17. No engine
dependencies, no networking library, no frontend framework. One binary that
serves its own UI and plays you at chess.

The move generator is verified against 414 million reference nodes. The
WebSocket layer implements RFC 6455 directly on BSD sockets — HTTP/1.1 upgrade,
SHA-1 accept key, frame masking, ping/pong — on a single-threaded, edge-triggered
`epoll` event loop.

```
browser ──── TCP ──── WebSocket (RFC 6455) ────▶  gambitd   (one process, one thread)
vanilla JS                                        ├─ epoll_wait loop, non-blocking fds
canvas board                                      ├─ http.cpp    GET / → index.html
latency HUD                                       │              Upgrade: websocket → 101
                                                  ├─ ws.cpp      SHA-1 + base64 accept key
                                                  │              frame codec, masking, ping/pong
                                                  ├─ server.cpp  session per fd, backpressure
                                                  └─ engine/     perft-verified movegen + search
```

## Quick start

Any OS with Docker — one build, one run, then open http://127.0.0.1:8080:

```sh
docker build -t gambitd .
docker run --rm -p 8080:8080 gambitd
```

Or, on Linux (or with Docker auto-detected elsewhere):

```sh
./run.sh     # then open http://127.0.0.1:8080
```

On Linux that compiles and runs the binary. Anywhere else it does the same
inside a throwaway `gcc:13` container, because the event loop uses `epoll` and
`epoll` is a Linux syscall. Knobs:

```sh
MOVETIME=100 ./run.sh    # weaker bot (less search time per move)
MOVETIME=3000 ./run.sh   # stronger
PORT=9000 ./run.sh
```

Or drive the binary yourself, on Linux:

```sh
make server
./gambitd --port 8080
```

Options: `--bind ADDR` (default `127.0.0.1`), `--port N`, `--movetime MS`
(default 500), `--public DIR`. Bind to `0.0.0.0` only inside a container; in
production leave it on loopback behind a TLS proxy.

No Linux handy? Docker works. On macOS or Linux:

```sh
docker run --rm -it -v "$PWD:/src" -w /src -p 8080:8080 gcc:13 bash -c \
  "make server && ./gambitd --bind 0.0.0.0 --port 8080"
```

On Windows the shell gets in the way, in two different ways.

From **Git Bash**, MSYS rewrites the container-side `/src` into a Windows path
before docker sees it, and `$PWD` is a `/c/...` path docker will not accept:

```sh
MSYS_NO_PATHCONV=1 docker run --rm -it -v "$(pwd -W):/src" -w /src -p 8080:8080 gcc:13 bash -c "make server && ./gambitd --bind 0.0.0.0 --port 8080"
```

From **PowerShell**, `${PWD}` must be braced or `$PWD:` parses as a drive
reference, and the command must stay on one line (`\` is not a continuation):

```powershell
docker run --rm -it -v "${PWD}:/src" -w /src -p 8080:8080 gcc:13 bash -c "make server && ./gambitd --bind 0.0.0.0 --port 8080"
```

## Testing

```sh
make test     # regression + protocol + search + fast perft   (~10s)
make deep     # full 414M-node perft reference suite          (~50s)
make fuzz     # coverage-guided frame-parser fuzzing (needs clang)
make bench    # latency benchmark against a running server
```

`./build.sh` does the same without make. On Windows run it from **Git Bash**
(`C:\Program Files\Git\bin\bash.exe`), not PowerShell — and note that the
`bash.exe` in `System32` is WSL's, not Git's. Every expected value in the test suite
is copied from an RFC or from published perft tables, never from a previous run
of this code — a test that asserts "whatever it did last time" proves nothing.

## Results

**Move generation** — [`perft`](https://www.chessprogramming.org/Perft) counts
leaf nodes of the legal-move tree and compares against published counts. A
generator that reproduces all six positions has pins, en-passant discovered
checks, castling rights, castling through check, and underpromotion right,
because each position was chosen to break one that does not.

| Position | Depth | Nodes | Exercises |
|---|---:|---:|---|
| startpos | 6 | 119,060,324 | baseline |
| Kiwipete | 5 | 193,690,690 | castling, pins, promotions |
| position3 | 6 | 11,030,083 | en passant, rook endgames |
| position4 | 4 | 422,333 | promotion under check |
| position5 | 5 | 89,941,194 | castling-rights edge cases |
| position6 | 4 | 3,894,594 | quiet middlegame |

```
PASSED 414117368 nodes in 47.92s  (8.64 Mnps)
```

**Move ordering** — the same alpha-beta search at fixed depth 5, with ordering
off and on. MVV-LVA capture ordering, killer moves, history heuristic, and
iterative-deepening PV ordering together:

| Position | No ordering | Ordered | Reduction |
|---|---:|---:|---:|
| startpos | 186,537 | 21,473 | 88.5% |
| Kiwipete | 1,305,009 | 86,445 | 93.4% |
| middlegame | 3,537,919 | 138,600 | 96.1% |
| **total** | **5,029,465** | **246,518** | **95.1%** |

The suite asserts that both configurations return the *same* score. Ordering may
only change how fast alpha-beta reaches the minimax value, never what that value
is. If those ever disagree, the pruning is unsound.

**Protocol** — SHA-1 against FIPS 180-1 vectors, base64 against RFC 4648, the
accept key against the RFC 6455 §1.3 worked example, frame bytes against §5.7.
Nine hostile-input cases (unmasked client frame, RSV bits, reserved opcodes,
fragmented control frames, non-minimal lengths, a declared 1 TiB payload) are
each rejected, and are also fired at a live server to confirm it hangs up rather
than trusting them.

**Fuzzing** — `tests/fuzz_ws.cpp` under libFuzzer with AddressSanitizer and
UBSan:

```
Done 18014673 runs in 46 second(s)     cov: 47 ft: 84     0 crashes
```

## Architecture

```
engine/    board, make/unmake, movegen, eval, search      no dependencies
net/       sha1, base64, RFC 6455 codec, HTTP             no dependencies, no sockets
net/server.cpp   the only file that touches epoll or a socket
tests/     perft, regression, search, protocol, fuzz harness
public/    index.html, app.js, style.css                  no build step
tools/     bench.py                                       no pip install
```

`sha1.cpp` and `ws.cpp` contain no socket calls, which is why the frame codec can
be fuzzed and unit-tested on any platform. The socket, the event loop, and the
session state live in exactly one file.

**Move generation** is pseudo-legal, then filtered by make/unmake:

```cpp
b.makeMove(m, u);
if (!b.attacked(b.kingSq(us), ~us)) out.add(m);
b.unmakeMove(m, u);
```

This is the slower of the two standard approaches, and the one that is correct by
construction. Absolute pins and the en-passant discovered check both fall out of
it for free; neither can be detected by inspecting a move's `from` and `to`
squares alone. Once profiling says the legality filter is hot, the fast path is
pin masks generated up front. Not before.

**Search** is negamax with alpha-beta, iterative deepening, MVV-LVA capture
ordering, killer moves, a history heuristic, and a quiescence search over
captures to remove horizon-effect blunders. Mate scores shrink with distance, so
a mate in two outranks a mate in five. Stalemate scores zero — a draw, not a loss.

**The event loop** is edge-triggered, so every `read` drains to `EAGAIN` and
every `accept` loops until the backlog is empty; edge-triggered `epoll` reports a
transition, not a level, and a single missed drain wedges the connection forever.
Partial writes are buffered and `EPOLLOUT` is armed until they land, so one slow
peer cannot stall the loop. `TCP_NODELAY` is set on every accepted socket:
Nagle's algorithm would hold a small move reply for up to 40 ms waiting on an ACK.

## Wire protocol

Newline-free JSON in WebSocket text frames.

| Direction | Message |
|---|---|
| → | `{"t":"new"}` |
| → | `{"t":"move","uci":"e2e4"}` |
| → | `{"t":"ping","id":7}` |
| ← | `{"t":"state","fen":"…","turn":"w","status":"check","last":"e7e5","eval":-30,"depth":6,"nodes":204800,"searchMs":312,"legal":["e2e4",…]}` |
| ← | `{"t":"pong","id":7}` |
| ← | `{"t":"error","msg":"illegal move"}` |

`ping` is answered before any chess work, so it measures the network path and the
event loop rather than the search. The server sends the full legal-move list, so
the client never needs its own rules engine — the browser cannot desync from the
server's idea of what is legal.

## Security

- **Client frames must be masked.** RFC 6455 §5.1 requires it; an unmasked frame
  is a protocol error and the connection is dropped. Masking exists so a poisoned
  intermediary cannot be tricked into caching a frame as an HTTP response.
- **Declared lengths are never trusted.** A 64-bit length field lets a client
  announce a 2^63-byte payload in eight bytes. Anything over `MAX_PAYLOAD` (1 MiB)
  is refused *before* any allocation.
- **Static assets are loaded into memory at startup** from a fixed whitelist, so
  no request path ever reaches the filesystem. Directory traversal is impossible
  by construction rather than by filtering.
- **Bounded buffers.** 8 KiB of headers, 1 MiB of unparsed input, 8 MiB of unsent
  output; past any of these the peer is dropped.
- **Binds to loopback by default.** Expose it through a TLS-terminating reverse
  proxy, not directly.
- **TLS is deliberately not implemented here.** Hand-rolling frame codecs is a
  protocol exercise with no security consequence. Hand-rolling TLS is how you
  ship a CVE. Terminate it at Caddy or nginx.

## Deploying

```
browser ──wss:443──▶ Caddy (TLS, Let's Encrypt)
                       └──ws://127.0.0.1:8080──▶ gambitd (systemd)
```

The entire Caddy config:

```
chess.example.com {
    reverse_proxy 127.0.0.1:8080
}
```

Caddy passes the `Upgrade` header through natively. nginx does not — you must set
`Upgrade` and `Connection` manually, which is a classic lost afternoon.

On EC2, put `443`/`80` in the security group, restrict `22` to your own `/32`, and
leave `gambitd` bound to `127.0.0.1` so it is reachable only through the proxy. A
security group is a stateful L3/L4 firewall; treat it like one.

For a public URL without running your own proxy, `fly.toml` deploys the
`Dockerfile` to Fly.io, which terminates TLS at the edge — `fly launch --no-deploy
--copy-config` once, then `fly deploy`. The app scales to zero when idle and
cold-starts on the next visit, which is fine for an occasional demo link.

## What is deliberately not here

No threads, no `io_uring`, no framework, no database, no auth, no matchmaking, no
transposition table, no opening book, no UCI interface. Each of those is a real
thing to add, and none of them is needed to make this correct or to make it fast
enough to play. The engine core stays free of the network layer so both stay
testable.

## Roadmap

- Transposition table with Zobrist hashing (the single biggest search win left)
- UCI interface, so strength can be measured properly against a reference engine
- Null-move pruning and late-move reductions
- Tapered evaluation with a distinct endgame king table
- Pin masks in movegen, once profiling justifies the complexity

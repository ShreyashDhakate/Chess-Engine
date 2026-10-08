# gambitd: Chess Engine & WebSocket Server

A browser-playable chess application backed by a C++17 engine and a WebSocket server implemented directly on Linux sockets. A single executable serves the frontend, validates moves and generates the bot's replies.

The project explores **chess search, protocol implementation and event-driven socket I/O** without an external chess engine, networking library or frontend framework.

[Source repository](https://github.com/ShreyashDhakate/Chess-Engine)

## Features

- Legal move generation with make/unmake validation, including castling, promotion and en passant.
- Negamax search with alpha-beta pruning, iterative deepening and quiescence.
- Move ordering using MVV-LVA, killer moves, history scores and principal-variation ordering.
- Server-side game state, legal moves and check/draw status sent to the browser.
- WebSocket HTTP upgrade, masking, control frames and fragmented-message handling.
- Nonblocking sockets on an edge-triggered `epoll` event loop.
- Buffered partial writes, bounded input/output buffers and protocol-error rejection.
- Reference perft tests, search/regression tests and a frame-parser fuzz harness.

## Architecture

| Layer | Files | Responsibility |
| --- | --- | --- |
| Chess engine | `engine/` | Board state, move generation, evaluation and search |
| Protocol codecs | `net/http.*`, `net/ws.*`, `net/sha1.*` | HTTP parsing, WebSocket framing and handshake support |
| Server | `net/server.cpp` | Socket lifecycle, `epoll`, per-connection state and request handling |
| Browser UI | `public/` | Board rendering, interaction and WebSocket messages |
| Verification | `tests/`, `tools/bench.py` | Correctness, fuzzing and latency measurement |

The server sends the authoritative board state and legal-move list. The browser does not need a separate chess rules engine.

Socket I/O is nonblocking, but **bot search runs synchronously on the same thread**. A long search can delay other connections. The current design is a compact interactive demo, not a claim of isolated search workers or high-concurrency serving.

## Getting started

### Option A: Docker

Requires Git and Docker running Linux containers.

```bash
git clone https://github.com/ShreyashDhakate/Chess-Engine.git
cd Chess-Engine
docker build -t gambitd .
docker run --rm -p 127.0.0.1:8080:8080 gambitd
```

Open [http://127.0.0.1:8080](http://127.0.0.1:8080). This option works on Linux, macOS and Windows with a compatible Docker installation.

Set the bot's search-time budget in milliseconds:

```bash
docker run --rm -p 127.0.0.1:8080:8080 -e MOVETIME=1000 gambitd
```

### Option B: Native Linux build

Requires a C++17 compiler and GNU Make. The server uses Linux `epoll`, so the native server build targets Linux.

```bash
git clone https://github.com/ShreyashDhakate/Chess-Engine.git
cd Chess-Engine
make server
./gambitd --bind 127.0.0.1 --port 8080 --movetime 500 --public public
```

The repository also includes `./run.sh`, which builds natively on Linux and uses Docker on supported non-Linux shells.

### Runtime options

| Option | Default | Purpose |
| --- | --- | --- |
| `--bind` | `127.0.0.1` | Listening interface |
| `--port` | `8080` | HTTP/WebSocket port |
| `--movetime` | `500` | Bot search budget in milliseconds |
| `--public` | `public` | Static asset directory |

The Docker image exposes equivalent `BIND`, `PORT` and `MOVETIME` environment variables. `BIND` defaults to `0.0.0.0` inside the container; the example host port mapping limits access to localhost.

## WebSocket protocol

The server accepts a WebSocket upgrade and exchanges JSON text messages.

### Client messages

```json
{"t":"new"}
```

```json
{"t":"move","uci":"e2e4"}
```

```json
{"t":"ping","id":7}
```

| Message | Behavior |
| --- | --- |
| `new` | Reset the current connection's game |
| `move` | Validate a UCI-formatted move, update the game and generate a bot reply |
| `ping` | Return the same ID in an application-level `pong` |

Server `state` messages include the FEN, turn, status, last move, evaluation, search depth, searched nodes, search time and legal moves. Illegal moves return an `error` message. WebSocket control ping/pong frames are handled separately from the JSON application-level ping.

## Testing

```bash
make test
make deep
```

| Target | Checks |
| --- | --- |
| `make test` | Move regressions, protocol vectors, search correctness and fast perft |
| `make deep` | Full reference perft suite |
| `make fuzz` | Frame-parser fuzzing with Clang/libFuzzer, AddressSanitizer and UBSan |
| `make bench` | Application ping latency against a running server |

`make fuzz` requires Clang with sanitizer/libFuzzer support. The core engine and protocol tests do not require a running server; benchmarks do.

### Move-generation coverage

The deep suite covers **414,117,368 reference leaf nodes** across six standard positions:

| Position | Depth | Reference nodes |
| --- | ---: | ---: |
| Initial position | 6 | 119,060,324 |
| Kiwipete | 5 | 193,690,690 |
| Position 3 | 6 | 11,030,083 |
| Position 4 | 4 | 422,333 |
| Position 5 | 5 | 89,941,194 |
| Position 6 | 4 | 3,894,594 |

Perft checks legal move-tree counts. It is a correctness check, not a playing-strength rating or server throughput benchmark.

The search test also compares ordering enabled/disabled at a fixed depth while asserting equal scores. The existing project notes record a reduction from **5,029,465** to **246,518** searched nodes over three depth-5 positions. Re-run `tests/test_search.cpp` through `make test` to reproduce this algorithm comparison.

## Latency measurements

Start the server in one terminal, then run:

```bash
python3 tools/bench.py --clients 8 --pings 200
python3 tools/bench.py --clients 1 --moves 10
```

The tool reports application ping round-trip percentiles. The move benchmark separately reports complete move round trips, including engine search, and the search duration returned by the server.

Record the machine, build flags, client count, search budget and workload when publishing a result. Ping latency cannot be presented as the latency of a full chess move, and `TCP_NODELAY` does not make all server paths allocation-free.

## Deployment

For a public deployment, keep the native process on loopback and terminate TLS at a reverse proxy such as Caddy or nginx. A minimal Caddy configuration is:

```caddyfile
chess.example.com {
    reverse_proxy 127.0.0.1:8080
}
```

Replace the example domain with a domain you control. The repository includes a Dockerfile and `fly.toml` as deployment starting points; their presence does not establish a currently running public service.

## Current scope

- No account system, multiplayer matchmaking or persistent game storage.
- Synchronous search on the networking thread; no worker pool.
- No UCI engine interface, opening book or transposition table yet.
- TLS is handled outside the application.
- Input/output and frame sizes are bounded; malformed protocol input is rejected.

Potential extensions include a search worker pool, UCI support, a Zobrist transposition table and stronger endgame evaluation.

## License

The repository currently has no root license file. Choose and add an explicit license before distributing the project under licensing terms.

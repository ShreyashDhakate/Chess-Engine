#!/usr/bin/env python3
"""Load generator and latency benchmark for gambitd.

Speaks RFC 6455 directly over asyncio streams -- no `websockets` package, no pip
install. The client-side masking here is the mirror image of the unmasking in
net/ws.cpp, so running this against the server exercises both directions.

  python tools/bench.py --clients 8 --pings 200
  python tools/bench.py --clients 1 --moves 10
"""

import argparse
import asyncio
import base64
import hashlib
import json
import os
import struct
import sys
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_TEXT = 0x1
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA


class WebSocket:
    """Minimal RFC 6455 client. Text frames only, no fragmentation on send."""

    def __init__(self, reader, writer):
        self.r = reader
        self.w = writer

    @classmethod
    async def connect(cls, host, port, path="/"):
        reader, writer = await asyncio.open_connection(host, port)
        # Latency measurements are meaningless if Nagle is coalescing our sends.
        sock = writer.get_extra_info("socket")
        if sock is not None:
            import socket as _s
            sock.setsockopt(_s.IPPROTO_TCP, _s.TCP_NODELAY, 1)

        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        writer.write(req.encode())
        await writer.drain()

        header = await reader.readuntil(b"\r\n\r\n")
        lines = header.decode("latin1").split("\r\n")
        if not lines[0].startswith("HTTP/1.1 101"):
            raise RuntimeError(f"handshake failed: {lines[0]!r}")

        # Verify the server derived the accept key correctly. A server that
        # echoes anything here is not implementing the protocol.
        want = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        got = ""
        for line in lines[1:]:
            if line.lower().startswith("sec-websocket-accept:"):
                got = line.split(":", 1)[1].strip()
        if got != want:
            raise RuntimeError(f"bad accept key: got {got!r}, want {want!r}")

        return cls(reader, writer)

    async def send(self, payload: str, opcode: int = OP_TEXT):
        data = payload.encode()
        n = len(data)
        frame = bytearray([0x80 | opcode])

        if n < 126:
            frame.append(0x80 | n)
        elif n <= 0xFFFF:
            frame.append(0x80 | 126)
            frame += struct.pack("!H", n)
        else:
            frame.append(0x80 | 127)
            frame += struct.pack("!Q", n)

        mask = os.urandom(4)
        frame += mask
        frame += bytes(b ^ mask[i & 3] for i, b in enumerate(data))

        self.w.write(bytes(frame))
        await self.w.drain()

    async def recv(self):
        """Returns (opcode, payload). Server frames are never masked."""
        b0, b1 = await self.r.readexactly(2)
        opcode = b0 & 0x0F
        masked = bool(b1 & 0x80)
        length = b1 & 0x7F

        if length == 126:
            (length,) = struct.unpack("!H", await self.r.readexactly(2))
        elif length == 127:
            (length,) = struct.unpack("!Q", await self.r.readexactly(8))

        if masked:
            raise RuntimeError("server must not mask frames (RFC 6455 s5.1)")

        payload = await self.r.readexactly(length) if length else b""
        return opcode, payload

    async def recv_text(self):
        while True:
            op, payload = await self.recv()
            if op == OP_TEXT:
                return payload.decode()
            if op == OP_PING:
                await self.send(payload.decode("latin1"), OP_PONG)
            elif op == OP_CLOSE:
                raise ConnectionError("server closed the connection")

    async def close(self):
        try:
            await self.send("", OP_CLOSE)
        except Exception:
            pass
        self.w.close()


def percentile(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    i = min(len(s) - 1, int(p / 100 * len(s)))
    return s[i]


def report(name, samples):
    if not samples:
        print(f"  {name:<16} no samples")
        return
    print(
        f"  {name:<16} n={len(samples):<6} "
        f"min={min(samples):7.3f}  p50={percentile(samples,50):7.3f}  "
        f"p99={percentile(samples,99):7.3f}  p999={percentile(samples,99.9):7.3f}  "
        f"max={max(samples):7.3f}   (ms)"
    )


async def ping_client(host, port, count, out):
    ws = await WebSocket.connect(host, port)
    await ws.recv_text()  # initial state pushed on upgrade

    for i in range(count):
        t0 = time.perf_counter()
        await ws.send(json.dumps({"t": "ping", "id": i}))
        while True:
            msg = json.loads(await ws.recv_text())
            if msg.get("t") == "pong" and msg.get("id") == i:
                break
        out.append((time.perf_counter() - t0) * 1000.0)
    await ws.close()


async def move_client(host, port, moves, out, search_out):
    ws = await WebSocket.connect(host, port)
    state = json.loads(await ws.recv_text())

    for _ in range(moves):
        legal = state.get("legal", [])
        if not legal or state.get("status") not in ("ongoing", "check"):
            break
        if state.get("turn") != "w":
            break

        t0 = time.perf_counter()
        await ws.send(json.dumps({"t": "move", "uci": legal[0]}))
        while True:
            msg = json.loads(await ws.recv_text())
            if msg.get("t") == "state":
                state = msg
                break
        out.append((time.perf_counter() - t0) * 1000.0)
        search_out.append(state.get("searchMs", 0))

    await ws.close()


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--clients", type=int, default=8)
    ap.add_argument("--pings", type=int, default=200)
    ap.add_argument("--moves", type=int, default=0, help="also time full move round-trips")
    args = ap.parse_args()

    print(f"gambitd bench -> {args.host}:{args.port}")
    print(f"  {args.clients} concurrent client(s), {args.pings} pings each\n")

    rtts = []
    t0 = time.perf_counter()
    await asyncio.gather(*[ping_client(args.host, args.port, args.pings, rtts) for _ in range(args.clients)])
    elapsed = time.perf_counter() - t0

    print("latency (application round-trip: JSON in, JSON out)\n")
    report("ping rtt", rtts)
    total = args.clients * args.pings
    print(f"\n  throughput       {total/elapsed:,.0f} req/s over {elapsed:.2f}s\n")

    if args.moves:
        moves, search = [], []
        await move_client(args.host, args.port, args.moves, moves, search)
        print("move round-trip (includes engine search)\n")
        report("move rtt", moves)
        report("server search", [float(x) for x in search])
        if moves and search:
            overhead = percentile(moves, 50) - percentile([float(x) for x in search], 50)
            print(f"\n  network+parse overhead at p50: {overhead:.3f} ms\n")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(130)

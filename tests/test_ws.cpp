// Protocol tests. Every expected value here is copied from an RFC, not from a
// previous run of this code -- a test that asserts "whatever it did last time"
// proves nothing.
//
//   SHA-1 vectors            RFC 3174 / FIPS 180-1
//   base64 vectors           RFC 4648 s10
//   accept-key example       RFC 6455 s1.3
//   frame byte sequences     RFC 6455 s5.7

#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../net/http.h"
#include "../net/sha1.h"
#include "../net/ws.h"

using namespace gambit::net;

static int failures = 0;

static void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::cout << (ok ? "  ok   " : "  FAIL ") << name;
    if (!ok && !detail.empty()) std::cout << "  -- " << detail;
    std::cout << "\n";
    if (!ok) ++failures;
}

static std::string hex(const std::array<uint8_t, 20>& d) {
    char buf[41];
    for (int i = 0; i < 20; ++i) std::snprintf(buf + i * 2, 3, "%02x", d[i]);
    return std::string(buf, 40);
}

static std::string b64(const std::string& s) {
    return base64(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

int main() {
    std::cout << "sha1 (RFC 3174 test vectors)\n\n";
    check(hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1(\"abc\")", hex(sha1("abc")));
    check(hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1(\"\")", hex(sha1("")));
    check(hex(sha1("The quick brown fox jumps over the lazy dog")) == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12",
          "sha1(\"The quick brown fox...\")");
    check(hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "sha1(56-byte message, exercises the pad-into-next-block path)");
    // 1,000,000 'a' -- forces many chunks and a >2^24 bit length.
    check(hex(sha1(std::string(1000000, 'a'))) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "sha1(1e6 x 'a')");

    std::cout << "\nbase64 (RFC 4648 s10)\n\n";
    check(b64("") == "", "base64(\"\")");
    check(b64("f") == "Zg==", "base64(\"f\")", b64("f"));
    check(b64("fo") == "Zm8=", "base64(\"fo\")", b64("fo"));
    check(b64("foo") == "Zm9v", "base64(\"foo\")", b64("foo"));
    check(b64("foob") == "Zm9vYg==", "base64(\"foob\")", b64("foob"));
    check(b64("fooba") == "Zm9vYmE=", "base64(\"fooba\")", b64("fooba"));
    check(b64("foobar") == "Zm9vYmFy", "base64(\"foobar\")", b64("foobar"));

    std::cout << "\nwebsocket handshake (RFC 6455 s1.3)\n\n";
    check(acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "accept key matches the RFC example",
          acceptKey("dGhlIHNhbXBsZSBub25jZQ=="));

    {
        const std::string req =
            "GET /chat HTTP/1.1\r\n"
            "Host: server.example.com\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        HttpRequest r;
        size_t used = 0;
        check(parseRequest(req.data(), req.size(), used, r) == Parse::Ok, "request parses");
        check(used == req.size(), "request consumes the whole header block");
        check(r.method == "GET" && r.path == "/chat", "request line split correctly");
        check(r.get("sec-websocket-key") == "dGhlIHNhbXBsZSBub25jZQ==", "header lookup is case-insensitive");
        check(isWebSocketUpgrade(r), "recognised as a websocket upgrade");

        // Truncation must never be mistaken for a complete request.
        for (size_t n = 0; n < req.size() - 1; ++n) {
            HttpRequest partial;
            size_t u = 0;
            if (parseRequest(req.data(), n, u, partial) != Parse::NeedMore) {
                check(false, "partial request at " + std::to_string(n) + " bytes returns NeedMore");
                break;
            }
        }
        check(true, "every truncation of the request returns NeedMore");
    }
    {
        // "Connection: keep-alive, Upgrade" is legal; a substring match on
        // "upgrade" would also wrongly accept "Connection: not-upgradeable".
        const std::string req =
            "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
            "Sec-WebSocket-Key: x\r\nSec-WebSocket-Version: 13\r\n\r\n";
        HttpRequest r;
        size_t used = 0;
        parseRequest(req.data(), req.size(), used, r);
        check(isWebSocketUpgrade(r), "comma-separated Connection token is accepted");
    }
    {
        const std::string req = "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: not-upgradeable\r\n"
                                "Sec-WebSocket-Key: x\r\nSec-WebSocket-Version: 13\r\n\r\n";
        HttpRequest r;
        size_t used = 0;
        parseRequest(req.data(), req.size(), used, r);
        check(!isWebSocketUpgrade(r), "'not-upgradeable' is not the 'upgrade' token");
    }
    {
        const std::string req = "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                "Sec-WebSocket-Key: x\r\nSec-WebSocket-Version: 8\r\n\r\n";
        HttpRequest r;
        size_t used = 0;
        parseRequest(req.data(), req.size(), used, r);
        check(!isWebSocketUpgrade(r), "version 8 is rejected (only 13 is supported)");
    }

    std::cout << "\nframe codec (RFC 6455 s5.7)\n\n";
    {
        // A single-frame masked text message "Hello" sent by a client.
        const unsigned char bytes[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
        Frame f;
        size_t used = 0;
        const Parse p = parseFrame(reinterpret_cast<const char*>(bytes), sizeof(bytes), used, f);
        check(p == Parse::Ok, "masked client frame parses");
        check(f.payload == "Hello", "unmasking recovers \"Hello\"", f.payload);
        check(f.fin && f.op == Opcode::Text, "fin and opcode decoded");
        check(used == sizeof(bytes), "consumed the whole frame");

        for (size_t n = 0; n < sizeof(bytes); ++n) {
            Frame pf;
            size_t u = 0;
            if (parseFrame(reinterpret_cast<const char*>(bytes), n, u, pf) != Parse::NeedMore) {
                check(false, "partial frame at " + std::to_string(n) + " bytes returns NeedMore");
                break;
            }
        }
        check(true, "every truncation of the frame returns NeedMore");
    }
    {
        // The corresponding unmasked server frame.
        const unsigned char want[] = {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f};
        const std::string got = buildFrame(Opcode::Text, "Hello");
        check(got.size() == sizeof(want) && std::memcmp(got.data(), want, sizeof(want)) == 0,
              "server frame bytes match the RFC");
    }
    {
        // Length boundaries: 125 inline, 126 uses 16 bits, 65536 uses 64 bits.
        check(buildFrame(Opcode::Text, std::string(125, 'x')).size() == 2 + 125, "len 125 stays in the 7-bit field");
        check(buildFrame(Opcode::Text, std::string(126, 'x')).size() == 4 + 126, "len 126 promotes to 16-bit");
        check(buildFrame(Opcode::Text, std::string(65536, 'x')).size() == 10 + 65536, "len 65536 promotes to 64-bit");
    }

    std::cout << "\nhostile input\n\n";
    auto expectError = [&](std::vector<unsigned char> b, const std::string& name) {
        Frame f;
        size_t used = 0;
        const Parse p = parseFrame(reinterpret_cast<const char*>(b.data()), b.size(), used, f);
        check(p == Parse::ProtocolError, name);
    };
    expectError({0x81, 0x05, 'H', 'e', 'l', 'l', 'o'}, "unmasked client frame is rejected");
    expectError({0xC1, 0x85, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "reserved bit RSV1 set is rejected");
    expectError({0x83, 0x85, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "reserved opcode 0x3 is rejected");
    expectError({0x89, 0xFE, 0x00, 0x7E, 0, 0, 0, 0}, "control frame longer than 125 bytes is rejected");
    expectError({0x09, 0x85, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "fragmented control frame is rejected");
    expectError({0x81, 0xFE, 0x00, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "non-minimal 16-bit length is rejected");
    expectError({0x81, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0, 0, 0, 0},
                "non-minimal 64-bit length is rejected");
    expectError({0x81, 0xFF, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "64-bit length with the high bit set is rejected");
    expectError({0x81, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0, 0, 0, 0},
                "payload above MAX_PAYLOAD is rejected before allocating");

    std::cout << "\nfuzz: 200k random buffers, parser must not crash or over-consume\n\n";
    {
        std::mt19937 rng(12345);  // fixed seed: a failure is reproducible
        size_t ok = 0, needMore = 0, err = 0;
        for (int iter = 0; iter < 200000; ++iter) {
            const size_t n = rng() % 24;
            std::vector<unsigned char> buf(n);
            for (auto& c : buf) c = (unsigned char)(rng() & 0xFF);
            Frame f;
            size_t used = 0;
            const Parse p = parseFrame(reinterpret_cast<const char*>(buf.data()), buf.size(), used, f);
            if (p == Parse::Ok) {
                ++ok;
                if (used > buf.size()) {
                    check(false, "parser consumed more than it was given");
                    break;
                }
                if (f.payload.size() > MAX_PAYLOAD) {
                    check(false, "parser produced an oversized payload");
                    break;
                }
            } else if (p == Parse::NeedMore) {
                ++needMore;
                if (used != 0) {
                    check(false, "NeedMore must consume nothing");
                    break;
                }
            } else {
                ++err;
            }
        }
        std::cout << "        ok=" << ok << " needMore=" << needMore << " protocolError=" << err << "\n";
        check(true, "fuzz completed without crash, over-consumption, or oversized payload");
    }

    std::cout << "\n" << (failures ? "FAILED" : "PASSED") << " -- " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}

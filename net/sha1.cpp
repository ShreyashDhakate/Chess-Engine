#include "sha1.h"

#include <cstring>
#include <vector>

namespace gambit::net {

namespace {

inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

}  // namespace

std::array<uint8_t, 20> sha1(const std::string& input) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    // Pad to a multiple of 64 bytes: a 0x80 byte, then zeros, then the original
    // bit length as a big-endian 64-bit integer.
    std::vector<uint8_t> msg(input.begin(), input.end());
    const uint64_t bitLen = uint64_t(input.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(uint8_t((bitLen >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = uint32_t(msg[chunk + i * 4]) << 24 | uint32_t(msg[chunk + i * 4 + 1]) << 16 |
                   uint32_t(msg[chunk + i * 4 + 2]) << 8 | uint32_t(msg[chunk + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = uint8_t((h[i] >> 24) & 0xFF);
        out[i * 4 + 1] = uint8_t((h[i] >> 16) & 0xFF);
        out[i * 4 + 2] = uint8_t((h[i] >> 8) & 0xFF);
        out[i * 4 + 3] = uint8_t(h[i] & 0xFF);
    }
    return out;
}

std::string base64(const uint8_t* data, size_t len) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        const uint32_t n = uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2];
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += T[(n >> 6) & 63];
        out += T[n & 63];
    }
    if (i < len) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < len) n |= uint32_t(data[i + 1]) << 8;
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += (i + 1 < len) ? T[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

}  // namespace gambit::net

#include "ws.h"

#include "sha1.h"

namespace gambit::net {

namespace {
// The magic string from RFC 6455 s1.3. It exists so that a server which merely
// echoes the request cannot accidentally look like it speaks WebSocket.
constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
}  // namespace

std::string acceptKey(const std::string& clientKey) {
    const auto digest = sha1(clientKey + WS_GUID);
    return base64(digest.data(), digest.size());
}

Parse parseFrame(const char* buf, size_t len, size_t& consumed, Frame& out) {
    consumed = 0;
    if (len < 2) return Parse::NeedMore;

    const uint8_t b0 = uint8_t(buf[0]);
    const uint8_t b1 = uint8_t(buf[1]);

    const bool fin = (b0 & 0x80) != 0;
    const uint8_t rsv = b0 & 0x70;
    const uint8_t opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    uint64_t payloadLen = b1 & 0x7F;

    // No extensions were negotiated, so any reserved bit set is a violation.
    if (rsv != 0) return Parse::ProtocolError;
    if (!masked) return Parse::ProtocolError;  // client MUST mask

    switch (opcode) {
        case 0x0: case 0x1: case 0x2: case 0x8: case 0x9: case 0xA: break;
        default: return Parse::ProtocolError;  // reserved opcode
    }

    const bool control = (opcode & 0x8) != 0;
    // Control frames carry <=125 bytes and are never fragmented (s5.5).
    if (control && (payloadLen > 125 || !fin)) return Parse::ProtocolError;

    size_t pos = 2;
    if (payloadLen == 126) {
        if (len < pos + 2) return Parse::NeedMore;
        payloadLen = (uint64_t(uint8_t(buf[pos])) << 8) | uint8_t(buf[pos + 1]);
        pos += 2;
        if (payloadLen < 126) return Parse::ProtocolError;  // must use minimal length
    } else if (payloadLen == 127) {
        if (len < pos + 8) return Parse::NeedMore;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i) payloadLen = (payloadLen << 8) | uint8_t(buf[pos + i]);
        pos += 8;
        // The high bit of a 64-bit length must be 0 (s5.2).
        if (payloadLen & (uint64_t(1) << 63)) return Parse::ProtocolError;
        if (payloadLen <= 0xFFFF) return Parse::ProtocolError;  // must use minimal length
    }

    if (payloadLen > MAX_PAYLOAD) return Parse::ProtocolError;

    if (len < pos + 4) return Parse::NeedMore;
    uint8_t mask[4];
    for (int i = 0; i < 4; ++i) mask[i] = uint8_t(buf[pos + i]);
    pos += 4;

    if (len < pos + payloadLen) return Parse::NeedMore;

    out.fin = fin;
    out.op = Opcode(opcode);
    out.payload.resize(size_t(payloadLen));
    for (uint64_t i = 0; i < payloadLen; ++i) out.payload[size_t(i)] = char(uint8_t(buf[pos + i]) ^ mask[i & 3]);

    consumed = pos + size_t(payloadLen);
    return Parse::Ok;
}

std::string buildFrame(Opcode op, const std::string& payload) {
    std::string f;
    f.reserve(payload.size() + 10);
    f += char(0x80 | uint8_t(op));  // FIN set; we never fragment outbound

    const size_t n = payload.size();
    if (n < 126) {
        f += char(uint8_t(n));
    } else if (n <= 0xFFFF) {
        f += char(126);
        f += char(uint8_t((n >> 8) & 0xFF));
        f += char(uint8_t(n & 0xFF));
    } else {
        f += char(127);
        for (int i = 7; i >= 0; --i) f += char(uint8_t((uint64_t(n) >> (i * 8)) & 0xFF));
    }
    f += payload;
    return f;
}

}  // namespace gambit::net

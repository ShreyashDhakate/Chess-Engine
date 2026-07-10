#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace gambit::net {

// A frame larger than this is refused outright. Without a cap, a client can
// announce a 2^63-byte payload in eight bytes and make the server allocate.
constexpr size_t MAX_PAYLOAD = 1u << 20;  // 1 MiB

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct Frame {
    bool fin = false;
    Opcode op = Opcode::Text;
    std::string payload;
};

enum class Parse {
    Ok,           // one frame decoded, `consumed` bytes taken from the buffer
    NeedMore,     // incomplete; wait for more bytes, consume nothing
    ProtocolError // malformed or hostile; the connection must be closed
};

// Decodes exactly one client->server frame. Client frames must be masked
// (RFC 6455 s5.1); an unmasked one is a protocol error, not a courtesy.
Parse parseFrame(const char* buf, size_t len, size_t& consumed, Frame& out);

// Encodes a server->client frame. Server frames are never masked.
std::string buildFrame(Opcode op, const std::string& payload);

// sha1(clientKey + GUID), base64-encoded. RFC 6455 s4.2.2.
std::string acceptKey(const std::string& clientKey);

}  // namespace gambit::net

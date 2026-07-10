#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace gambit::net {

// SHA-1 (RFC 3174). Used only to derive the WebSocket accept key, which is a
// handshake formality, not a security primitive -- RFC 6455 section 1.3 says so
// explicitly. Do not reach for this for anything that needs to be secure.
std::array<uint8_t, 20> sha1(const std::string& input);

// Base64 (RFC 4648) encode.
std::string base64(const uint8_t* data, size_t len);

}  // namespace gambit::net

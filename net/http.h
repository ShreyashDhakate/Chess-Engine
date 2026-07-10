#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "ws.h"  // for Parse

namespace gambit::net {

// A request whose headers exceed this is dropped. An unbounded header buffer is
// a trivial memory-exhaustion vector on a public listener.
constexpr size_t MAX_HEADER_BYTES = 8192;

struct HttpRequest {
    std::string method;
    std::string path;
    std::vector<std::pair<std::string, std::string>> headers;

    // Header names are case-insensitive (RFC 7230 s3.2).
    std::string get(const std::string& name) const;
};

// Returns Ok once the full header block (terminated by CRLFCRLF) is present.
Parse parseRequest(const char* buf, size_t len, size_t& consumed, HttpRequest& out);

// True when this is a well-formed RFC 6455 opening handshake.
bool isWebSocketUpgrade(const HttpRequest& r);

std::string handshakeResponse(const std::string& clientKey);
std::string httpResponse(const std::string& status, const std::string& contentType, const std::string& body);

}  // namespace gambit::net

#include "http.h"

#include <algorithm>
#include <cctype>

namespace gambit::net {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool containsToken(const std::string& headerValue, const std::string& token) {
    const std::string hv = lower(headerValue);
    const std::string t = lower(token);
    size_t pos = 0;
    while ((pos = hv.find(t, pos)) != std::string::npos) {
        const bool leftOk = (pos == 0) || hv[pos - 1] == ',' || hv[pos - 1] == ' ';
        const size_t end = pos + t.size();
        const bool rightOk = (end == hv.size()) || hv[end] == ',' || hv[end] == ' ';
        if (leftOk && rightOk) return true;
        pos = end;
    }
    return false;
}

}  // namespace

std::string HttpRequest::get(const std::string& name) const {
    const std::string want = lower(name);
    for (const auto& [k, v] : headers)
        if (lower(k) == want) return v;
    return "";
}

Parse parseRequest(const char* buf, size_t len, size_t& consumed, HttpRequest& out) {
    consumed = 0;
    const std::string data(buf, len);
    const size_t end = data.find("\r\n\r\n");
    if (end == std::string::npos) {
        return (len > MAX_HEADER_BYTES) ? Parse::ProtocolError : Parse::NeedMore;
    }
    if (end + 4 > MAX_HEADER_BYTES) return Parse::ProtocolError;

    size_t lineStart = 0;
    size_t lineEnd = data.find("\r\n");
    const std::string requestLine = data.substr(0, lineEnd);

    // "GET /path HTTP/1.1"
    const size_t sp1 = requestLine.find(' ');
    if (sp1 == std::string::npos) return Parse::ProtocolError;
    const size_t sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return Parse::ProtocolError;

    out.method = requestLine.substr(0, sp1);
    out.path = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    out.headers.clear();

    lineStart = lineEnd + 2;
    while (lineStart < end) {
        lineEnd = data.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > end) break;
        const std::string line = data.substr(lineStart, lineEnd - lineStart);
        const size_t colon = line.find(':');
        if (colon != std::string::npos)
            out.headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
        lineStart = lineEnd + 2;
    }

    consumed = end + 4;
    return Parse::Ok;
}

bool isWebSocketUpgrade(const HttpRequest& r) {
    if (r.method != "GET") return false;
    if (!containsToken(r.get("Upgrade"), "websocket")) return false;
    if (!containsToken(r.get("Connection"), "upgrade")) return false;
    if (r.get("Sec-WebSocket-Version") != "13") return false;
    if (r.get("Sec-WebSocket-Key").empty()) return false;
    return true;
}

std::string handshakeResponse(const std::string& clientKey) {
    return "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: " + acceptKey(clientKey) + "\r\n\r\n";
}

std::string httpResponse(const std::string& status, const std::string& contentType, const std::string& body) {
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "X-Content-Type-Options: nosniff\r\n\r\n" + body;
}

}  // namespace gambit::net

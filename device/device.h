#pragma once
#include <string>
#include <string_view>
#include <optional>

// I/O device abstraction — identical API to device/*.go and device/*.rs.

namespace kvlang::device {

// File I/O
std::string read_file(std::string_view path);
void write_file(std::string_view path, std::string_view text);

// Terminal I/O (stdin/stdout)
std::optional<std::string> read_term(std::string_view prompt);
void write_term(std::string_view text);

// WebSocket
class WSConn {
public:
    virtual ~WSConn() = default;
    virtual void write(std::string_view msg) = 0;
    virtual std::string read() = 0;
};

} // namespace kvlang::device

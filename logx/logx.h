#pragma once
#include <string>
#include <string_view>
#include <vector>

// Logging — identical API to logx/logx.go and logx/logx.rs.

namespace kvlang::logx {

enum class Level { debug, info, warn, error, fatal };

void set_level(Level lv);
Level get_level();

void debug(std::string_view fmt, auto&&... args)   { /* TODO */ }
void info(std::string_view fmt, auto&&... args)    { /* TODO */ }
void warn(std::string_view fmt, auto&&... args)    { /* TODO */ }
void error(std::string_view fmt, auto&&... args)   { /* TODO */ }
[[noreturn]] void fatal(std::string_view fmt, auto&&... args);

// Diagnostic printing (from parser)
struct Diagnostic {
    int line, col;
    std::string message;
    bool warn, info;
    std::string source_line;
    std::string src_file;
    std::string src_name;
};
void diag(const Diagnostic& d);
bool has_errors(const std::vector<Diagnostic>& diags);

} // namespace kvlang::logx

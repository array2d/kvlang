// kvlang C++ CLI — entry point for "run" command.
//
// Usage: kvlang run <file.kv>
//
// This is the C++ counterpart to cmd/kvlang/main.go (Go toolchain CLI)
// and cmd/kvlang/main.rs (Rust runtime CLI).

#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: kvlang run <file.kv>\n";
        return 1;
    }

    std::string_view cmd = argv[1];
    if (cmd == "run") {
        // TODO: parse → lower → layout → execute
        std::cout << "[cpp] run: not yet implemented\n";
        return 0;
    }

    std::cerr << "unknown command: " << cmd << "\n";
    return 1;
}

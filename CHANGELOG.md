# Changelog

## [0.1.0] — 2026-07-15

### Added
- KV path-addressed declarative VM interpreter
- Typed values: int, float, bool, str, bytes, tensor
- Builtin operators: arithmetic, comparison, logic, cast, call, string, I/O
- Tail-call optimization (TCO) for deep recursion
- Frame isolation with scope correctness
- Soft links (symlink-like path redirection)
- `vthread` coroutine model with concurrent workers
- `kvspace` CLI: get, set, del, list, clear, watch, notify
- `serve` daemon mode with Redis-backed persistent state
- `load` / `vet` / `format` subcommands
- Pipe and inline (`-c`) execution modes
- 179 example programs (P0–P3 graded)
- Automated integration test suite (175 cases)

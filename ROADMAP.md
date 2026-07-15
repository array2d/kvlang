# Roadmap

## v0.1 (current) — Core VM

- [x] KV path-addressed instruction model
- [x] typed values: int, float, bool, str, bytes, tensor
- [x] builtin operators: arith, compare, logic, cast, call, io
- [x] TCO (tail-call optimization)
- [x] vthread coroutine scheduler (128 workers)
- [x] soft links (path redirection)
- [x] serve daemon with Redis persistence
- [x] pipe/inline execution modes

## v0.2 — Developer Experience

- [ ] structured tutorial (tutorial/)
- [ ] playground / online demo
- [ ] language server (LSP) for IDE support
- [ ] improved error messages with source locations
- [ ] `go install` support

## v0.3 — Ecosystem

- [ ] package manager (`kvlang get`)
- [ ] standard library: http, json, file I/O
- [ ] foreign function interface (FFI)
- [ ] benchmarking suite

## v0.4 — Distributed

- [ ] multi-node vthread scheduling
- [ ] kvspace sharding
- [ ] RDMA backend for kvspace
- [ ] cluster mode (`kvlang serve --cluster`)

## Future

- [ ] GPU tensor ops (via Triton)
- [ ] WASM compile target
- [ ] self-hosting compiler (kvlang → kvlang)

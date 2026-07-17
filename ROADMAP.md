# Roadmap

## v0.1.0 — Core VM ✅

- [x] KV path-addressed instruction model (`[i,0]` opcode, `[i,-j]` reads, `[i,+j]` writes)
- [x] typed values: int, float, bool, str, bytes, tensor
- [x] builtin operators: arith, compare, logic, bitwise, cast, call, string, I/O
- [x] single-layer IR — same syntax is VM instruction, high-level language, and compiler IR
- [x] control flow: if/else, while (break/continue), for-in
- [x] lower pass: if/while → block + br/goto
- [x] TCO (tail-call optimization) for deep recursion
- [x] multi-return values + discard (`_`)
- [x] vthread coroutine model with concurrent workers
- [x] soft links (path redirection)
- [x] serve daemon with Redis persistence
- [x] pipe / inline (`-c`) / file execution modes
- [x] `kvspace` CLI: get, set, del, list, tree, clear, watch, notify
- [x] `vet` (syntax check) + `format` (source formatter)
- [x] 87 tutorial examples: basics, functions, control flow, algorithms, LeetCode
- [x] CI: build (Linux + macOS), tutorial tests (Redis), cross-compile releases

## v0.2.0 — Ecosystem Foundations

- [ ] playground / online demo
- [ ] improved error messages with source locations
- [ ] `go install` support
- [ ] package manager (`kvlang get`)
- [ ] standard library: http, json, file I/O

## v0.3.0 — Tensor & GPU

- [ ] tensor lifecycle ops: tensor.new / tensor.del / tensor.clone (heap-plat backed)
- [ ] tensor compute dispatch via kvspace op-plat routing
- [ ] kvspace-cpp client library for C++ backends
- [ ] GPU tensor ops (Triton / CUDA integration)
- [ ] benchmarking suite

## v0.4.0 — Distributed

- [ ] multi-node vthread scheduling
- [ ] kvspace sharding
- [ ] RDMA backend for kvspace
- [ ] cluster mode (`kvlang serve --cluster`)

## Future

- [ ] self-hosting compiler (kvlang → kvlang)
- [ ] WASM compile target
- [ ] foreign function interface (FFI)

# Contributing to kvlang

Thanks for your interest! Here's how to get started.

## Setup

```bash
git clone https://github.com/array2d/kvlang.git
cd kvlang
make build
```

**Prerequisites**: Go 1.24+, Redis (for integration tests).

## Development workflow

```bash
# After making changes:
make vet          # static analysis
make test         # unit tests
python3 run.py             # integration tests (requires Redis)
```

## Project structure

```
cmd/kvlang/         CLI entry point
internal/
  parser/           .kv source → AST
  lower/            control flow lowering (if/while → block + branch)
  layoutcode/       AST → KV path tree (opcodes at /vthread/*)
  vthread/          virtual thread lifecycle
  kvcpu/            128-worker goroutine scheduler
  kvspace/          KV storage abstraction (Redis backend)
  vtype/            typed value system (int, float, bool, str, tensor)
  op/
    builtin/        native operators (arith, compare, logic, cast, call, io)
    dispatch/       opcode → executor routing
  device/           I/O device drivers (terminal, websocket)
tutorial/           step-by-step .kv programs (01-hello … 08-algo)
run.py              integration test suite (~50 tests)
doc/                design documents, specs, drafts
```

## Key concepts

- **KV path addressing**: code and data share one tree. Instructions are paths. Call = subtree copy.
- **vthread**: lightweight execution context. State stored as KV paths under `/vthread/<vtid>/`.
- **typed values**: `kvspace.Value{kind, raw}` — kind maps directly to `vtype.VType.Name()`.

## Commit conventions

- `feat:` new feature
- `fix:` bug fix
- `refactor:` code restructuring
- `docs:` documentation
- `test:` test additions

## Questions?

Open an issue or start a discussion.

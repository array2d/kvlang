# oldhero — the Go-era kvlang

This directory holds the **original Go implementation** of kvlang (back when it
was still called dxlang). It was the first working end-to-end system: parser,
lower, symbol, rwir, kvcpu, vthread, keytree, the rwir extension host, and an
early stdlib — the whole pipeline, in Go.

```
ast/  cmd/  keytree/  kvcpu/  layout/  logx/  lower/
parser/  rwir/  rwirext/  stdlib/  symbol/  vthread/
```

## Status: archived, frozen

The live implementation has moved on and now lives at the repository root:

- **layout** — a Rust crate (`layout/`) producing `libkvlanglayout.so` + `kvlanglayout`
- **runtime** — C (`runtime/`) producing `libkvlang_runtime.so`, driven by the
  Rust binary `kvlang`

This Go version is kept **only as a memorial to the old hero** that carried the
project through its first era. It is **not maintained and will not be updated**.
For anything current — language behavior, the KV-tree model, the two-stage
layout/runtime split — refer to the root implementation and `stdlib/kvlang`, not
this directory.

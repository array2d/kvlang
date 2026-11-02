# runtimec architecture

This directory expands the rules in `runtimec/README.md`:

- `runtimec-architecture.md` defines ownership, the only execution trunk, and failure rules.
- `../../specification/five-language-behavior-matrix.md` is the single source of language behavior decisions.
- `capability-map.md` maps required capability to one final owner.
- `runtimec-trunk.mmd` shows the only component trunk.
- `runtimec-execution.mmd` shows vmjit refusal and vmbin fallback.
- `runtimec-dependencies.mmd` shows allowed compile-time dependencies.
- `rwir-binrwir-aliasing.mmd` shows explicit Read/Write aliasing.

Mermaid files are sources. Rendered image files are review aids and must be regenerated when
their matching Mermaid source changes.

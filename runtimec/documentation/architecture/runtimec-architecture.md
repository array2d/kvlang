# runtimec Architecture Contract

## Purpose

runtimec is the single C11 runtime for kvlang. This document fixes component ownership and
failure direction while language semantics remain unimplemented. A convenient implementation
must never override these rules.

## Only execution trunk

```text
public interface
    -> engine route
    -> vmkv reads and compiles RWIR
    -> immutable BinRWIR Program
    -> vmjit try
       -> executed
       -> fallback to vmbin
    -> route commits effects and checkpoint
```

`engine/route` is the only policy owner. vmbin is the only correctness executor. vmjit is a
removable accelerator that may refuse a Program. Generic, typed, fused, and continuous
execution are internal vmbin handler forms, never separate executors.

## Ownership

| Component | Owns | Must not own |
|---|---|---|
| `include/kvlang/runtimec.h` | public status, request, result, error, opaque runtime | internal component structures |
| `interface/` | request translation and exact exported symbols | execution policy or language semantics |
| `contract/` | shared status helpers, errors, ownership and host port types | vmkv, vmbin, or vmjit policy |
| `host/` | concrete kvspace and external capability adapters | BinRWIR or execution selection |
| `vmkv/` | RWIR reading, validation, type resolution, Program building | execution, machine code, checkpoint commit |
| `vmbin/` | BinRWIR, Program, Frame, Binding, Cell and complete interpretation | vmjit selection or persistent truth |
| `vmjit/` | verification, lowering and optional machine code execution | RWIR parsing or language semantics |
| `engine/route` | execution choice, fallback, effect commit and checkpoint | opcode semantics |
| `specification/` | the single language-decision matrix and RWIR operation manifests | execution strategy |

## Facts and derived state

kvspace plaintext is the only durable semantic source. RWIR, values, frame facts, source
program counter, status, error and recovery state belong there. BinRWIR Program data,
register caches, dispatch tables, analysis plans, machine code and profiles are derived and
must be safe to delete.

A finalized Program is immutable. Mutable call bindings, profiles, code caches and fusion
plans are sidecars with explicit session ownership.

## BinRWIR contract

BinRWIR preserves RWIR variable-arity semantics. Compilation fixes each instruction's read and
write counts and stores references in this order:

```text
[ReadReference ...][WriteReference ...]
```

The common form stays compact. A wide form may use a cold side table. Both forms must produce
the same normalized view for vmbin and vmjit. Encoding limits never justify truncating operands
or splitting one atomic language operation.

Read and write parameters may explicitly bind the same canonical Cell. Signature owns the only
alias policy. Distinct register identifiers preserve access authority even when their Bindings
point to the same Cell. Undeclared aliasing is invalid.

## vmjit refusal

vmjit consumes only a validated, immutable Program and execution state. It may return fallback
for unsupported operations, cold code, disabled machine code, platform limits, failed guards,
or inability to preserve an observable boundary. Fallback means vmjit did not complete the
instruction; route resumes vmbin without repeating a committed effect.

## Observation and commit

Every source instruction boundary remains observable and recoverable. An implementation may
keep values in derived storage within a valid region, but route commits only complete effects.
Commit ordering is values, source program counter, then status or error. No component may make
a partial instruction externally visible.

## Status direction

`RUNTIMEC_STATUS_SUCCESS` is the only normal completion. `FALLBACK` is reserved for vmjit
refusal. `NOT_IMPLEMENTED` reports absent scaffold behavior. `INVALID` reports input outside
the contract. Outputs enter a documented empty or invalid state before work starts.

## Behavior decisions

Every language behavior starts in `../../specification/five-language-behavior-matrix.md`. A complete row records C,
Python, Rust, Go, and V8/TypeScript behavior, then chooses consensus or a stated camp with a
boundary fixture and invalid-input rule. Product semantics must carry a matching
`behavior-decision: <decision>` marker. No matrix means no manifest entry and no execution.

## Compile-time dependencies

```text
interface -> public + contract + engine
engine/route -> host + vmkv + BinRWIR + vmjit + vmbin
vmkv -> contract + BinRWIR build contract
vmbin -> contract + BinRWIR
vmjit -> contract + read-only BinRWIR contract
host -> contract + external kvspace interface
```

vmkv never depends on vmjit. vmbin never depends on vmjit. vmjit never depends on vmkv or a
concrete host implementation. No engine file other than route may depend on execution
components.

## Acceptance

The scaffold is acceptable only when:

- the one public header compiles as C11 and C++17;
- only the three structured public functions are exported;
- all unimplemented calls fail explicitly and clear outputs;
- missing component sources stop CMake configuration;
- name, dependency, behavior-matrix and export checks pass;
- no third executor, old path, abbreviated project name or alternate status survives;
- a fresh build and all scaffold tests pass.

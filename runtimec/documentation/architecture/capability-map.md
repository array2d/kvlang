# runtimec Capability Ownership

Each capability has one final owner. A capability may use lower contracts but must not create
another execution path.

| Capability | Final owner | Required direction |
|---|---|---|
| Public create, execute, destroy | `interface/` | structured request and result only |
| Status and error vocabulary | `include/kvlang/runtimec.h`, `contract/` | success is the only normal completion |
| kvspace access and checkpoint storage | `host/` | available only through narrow host ports |
| Execution policy and commit ordering | `engine/route.c` | vmjit try, then vmbin fallback |
| RWIR reading and validation | `vmkv/` | plaintext to Program builder |
| Type resolution and instruction specialization | `vmkv/` | based on specification decisions |
| Immutable BinRWIR Program | `vmbin/program.*` | one read-only representation for both executors |
| Read/Write references and alias policy | `vmbin/reference.*`, `vmbin/signature.*` | Signature owns policy; Binding proves identity |
| Complete language execution | `vmbin/interpreter.*` | all valid BinRWIR remains executable |
| Internal typed, fused and continuous handlers | `vmbin/` | private strategy behind `vmbin_execute` |
| Machine code eligibility | `vmjit/verifier.h` | refusal returns fallback |
| Target-neutral lowering | `vmjit/lower.h` | consumes verified BinRWIR only |
| x86_64 and AArch64 generation | `vmjit/backend/` | peer implementations of one backend contract |
| Language behavior decisions | `specification/five-language-behavior-matrix.md` | the single decision source precedes manifest and code |
| RWIR operation facts | `specification/` | one generated source for vmkv, vmbin and vmjit |
| Exported symbol set | `interface/exports.txt` | exact match required on every platform |

## Rejected ownership

- vmjit does not own language semantics or fallback policy.
- vmkv does not execute or generate machine code.
- host does not inspect BinRWIR instructions.
- Program does not own mutable profiles, code caches or fusion plans.
- No component owns another baseline executor.
- No public header exposes internal runtimec structures.

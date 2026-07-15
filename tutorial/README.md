# kvlang Tutorial

A progressive, step-by-step guide to kvlang. Each directory contains a standalone `main.kv` you can run immediately.

| Step | Topic | What you'll learn |
|------|-------|-------------------|
| [01-hello](01-hello/) | Hello World | `print`, terminal activation |
| [02-vars](02-vars/) | Variables | path-based read/write (`'./x'`) |
| [03-arith](03-arith/) | Arithmetic | `+ - * /`, `pow`, `sqrt`, `abs`, `max`, `min` |
| [04-func](04-func/) | Functions | `def`, call, arguments, return values |
| [05-if](05-if/) | Control Flow | `if/else`, boolean logic |
| [06-recursion](06-recursion/) | Recursion | multi-return, tail-call optimization |

## Running

```bash
kvlang tutorial/01-hello/main.kv
kvlang tutorial/02-vars/main.kv
# ... etc
```

Each file is self-contained and prints its own output. Read the comments in each `main.kv` for line-by-line explanations.

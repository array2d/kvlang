# kvlang Tutorial

A progressive, step-by-step guide to kvlang.  
Each numbered directory contains a standalone `main.kv` you can run immediately.  
`08-algo/` is a collection of complete programs — run any file directly.

## Steps

| Step | Topic | What you'll learn |
|------|-------|-------------------|
| [01-hello](01-hello/) | Hello World | `print`, terminal activation |
| [02-vars](02-vars/) | Variables | path-based read/write (`./x`) |
| [03-arith](03-arith/) | Arithmetic | `+ - * /`, `pow`, `sqrt`, `abs`, `max`, `min` |
| [04-func](04-func/) | Functions | `def`, call, arguments, write params |
| [05-if](05-if/) | Conditionals | `if/else`, boolean operators `&& \|\| !` |
| [06-while](06-while/) | While Loops | `while`, `break`, `continue` |
| [07-recursion](07-recursion/) | Recursion | multi-write params, tail-call optimization |
| [08-algo/](08-algo/) | Algorithms | fibonacci, fizzbuzz, gcd, collatz, … |

## Running

```bash
# numbered steps — each prints its own output
kvlang tutorial/01-hello/main.kv
kvlang tutorial/06-while/main.kv
# ...

# algo showcase — any file is self-contained
kvlang tutorial/08-algo/fibonacci.kv   # fib = 55
kvlang tutorial/08-algo/fizzbuzz.kv    # FizzBuzz 1-15
kvlang tutorial/08-algo/gcd.kv         # gcd = 6
kvlang tutorial/08-algo/collatz.kv     # steps = 111
```

## Algorithms in 08-algo

| File | Algorithm | Key concepts |
|------|-----------|--------------|
| `fibonacci.kv` | Iterative Fibonacci | while, variable swap |
| `factorial.kv` | Iterative Factorial | while accumulator |
| `fizzbuzz.kv` | FizzBuzz 1–15 | modulo, nested if |
| `gcd.kv` | GCD (Euclidean) | tail recursion |
| `power.kv` | Fast power (loop) | while, counter |
| `classify.kv` | Grade classifier | nested if, str.set |
| `collatz.kv` | Collatz conjecture | while, even/odd branch |
| `tco_depth.kv` | TCO verification | while → goto, tail recursion |
| `scope_isolation.kv` | Frame isolation | multi-write params, call isolation |

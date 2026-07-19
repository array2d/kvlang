# Title

**KVLang: A programming language where execution state lives entirely in key-value storage — built for AI agents to generate and run code (not humans)**

---

# Body

Most programming languages are designed around a single assumption: *a human reads and writes the code.*

I'm building one that's different. **KVLang** is designed to be *generated and executed by AI agents* — where every variable, stack frame, and function definition lives at a named path in a KV store. The storage layer is an abstract **kvspace** interface; Redis is the current implementation, but any KV backend that supports the interface works.

---

## What makes it different

In KVLang, the `->` operator writes a result *to a path*:

```kv
1 + 2 -> ./x
./x * 3 -> ./y
print("result =", ./y)
```

Run directly with no boilerplate — no `main()`, no `import`, no file required:

```bash
$ kvlang -c '1 + 2 -> ./x; ./x * 3 -> ./y; print("result =", ./y)'
result = 6
```

`./x` isn't a local variable in the traditional sense. It's a path in your process's execution frame — and that frame is a subtree in kvspace. Every value the program touches is a key you can inspect, watch, or override from outside.

---

## Functions are registered in KV space

```kv
def fibonacci(n: int) -> (result: int) {
    if (n <= 1) {
        n + 0 -> ./result
    } else {
        0 -> ./a
        1 -> ./b
        2 -> ./i
        while (./i <= n) {
            ./a + ./b -> ./c
            ./b + 0 -> ./a
            ./c + 0 -> ./b
            ./i + 1 -> ./i
        }
        ./b + 0 -> ./result
    }
}

fibonacci(10) -> ./ans
print("fib =", ./ans)
```

```bash
$ kvlang fibonacci.kv
fib = 55
```

When you load this file, `fibonacci` gets registered at `/func/fibonacci/` in kvspace. Another agent can call it by name, inspect its source, or override its behavior — all through KV operations.

---

## The execution model is built for agents

**Agents don't think in programs. They think in tasks.**

- A single top-level expression is a complete task — no entry point ceremony
- Multiple concurrent agents = multiple *vthreads* (lightweight, scheduled by the VM, state in kvspace)
- Every stack frame is a KV subtree: inspectable, restartable, migratable
- Functions are a shared library — one agent defines `add()`, every other agent can call it

This is the opposite of how most languages work. Most languages assume a program runs, completes, and disappears. KVLang assumes the execution *environment persists* and agents drop in and out of it.

---

## Where it is right now

**Honest status: early but functional.**

✅ Parser with full expression precedence (Pratt parser)  
✅ if / while / for / recursion / tail calls  
✅ Arithmetic, comparison, logic, string, print builtins  
✅ Concurrent vthread scheduling (worker pool + kvspace queues)  
✅ Pluggable kvspace backend (Redis implementation available)  
✅ Example programs run correctly  

🚧 Not yet validated by a large-scale KV project  
🚧 LLM/agent builtins (`llm.call`, `session.*`) not yet wired  
🚧 `for` iteration over KV paths — designed, not implemented  

The immediate next step is connecting it to an agent runtime (livebyte) that uses KVLang as its execution engine for LLM tool-call loops, context compaction, and multi-agent teams.

---

## The core bet

LLMs generate code that is run once, immediately, in a throwaway environment. That's fine for scripts. But for *agents that persist across sessions, share state, and compose with other agents* — you want the execution environment to be a database, not a process.

KVLang makes that the default, not an afterthought.

Source: [github.com/array2d/kvlang](https://github.com/array2d/kvlang)  
Feedback welcome — especially if you've thought about agent-native execution models.

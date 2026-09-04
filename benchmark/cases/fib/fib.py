import os
import time


def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


t0 = time.perf_counter_ns()
ans = fib(int(os.environ["BENCH_SCALE"]))
t1 = time.perf_counter_ns()
print("fib =", ans)
print("__bench_ns:", t1 - t0)

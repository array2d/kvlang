import os
import time


def iops(n: int) -> int:
    a = 0
    i = 1
    while i <= n:
        a = a + 1
        i = i + 1
    return a


t0 = time.perf_counter_ns()
ans = iops(int(os.environ["BENCH_SCALE"]))
t1 = time.perf_counter_ns()
print("iops a =", ans)
print("__bench_ns:", t1 - t0)

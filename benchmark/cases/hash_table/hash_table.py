import os
import time

n = int(os.environ["BENCH_SCALE"])
h = {}
t0 = time.perf_counter_ns()
for i in range(n):
    key = (i * 2654435761) % 100003
    h[key] = i
total = 0
hits = 0
for i in range(n):
    key = (i * 2654435761) % 100003
    if key in h:
        hits += 1
        total += h[key]
t1 = time.perf_counter_ns()
print("hash: hits =", hits, "sum =", total)
print("__bench_ns:", t1 - t0)

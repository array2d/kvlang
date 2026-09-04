import os
import time

base = "GGTATTGAGCACTGGCAATTGACGTCAGGTATCCGAATTGCACGTTAGCATGCATGCATGCACGT"
s = base * int(os.environ["BENCH_SCALE"])
h = {}
t0 = time.perf_counter_ns()
for ch in s:
    c = ord(ch)
    h[c] = h.get(c, 0) + 1
t1 = time.perf_counter_ns()
print(
    "knuc: A =", h[65], "C =", h[67], "G =", h[71], "T =", h[84]
)
print("__bench_ns:", t1 - t0)

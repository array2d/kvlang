import os
import time

n = int(os.environ["BENCH_SCALE"])
A = [0.0] * (n * n)
B = [0.0] * (n * n)
for i in range(n):
    for j in range(n):
        idx = i * n + j
        A[idx] = float((i + j) % 4) * 0.25 + 0.25
        B[idx] = float((i * 2 + j) % 4) * 0.25 + 0.25
t0 = time.perf_counter_ns()
checksum = 0.0
for i in range(n):
    for j in range(n):
        acc = 0.0
        for k in range(n):
            acc += A[i * n + k] * B[k * n + j]
        checksum += acc
t1 = time.perf_counter_ns()
out = int(checksum * 1000000.0)
print("matmul: check =", out)
print("__bench_ns:", t1 - t0)

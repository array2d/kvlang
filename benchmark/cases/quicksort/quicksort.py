import os
import time
import sys

sys.setrecursionlimit(10000)


def qsort(a, lo, hi):
    if lo < hi:
        pivot = a[hi]
        i = lo - 1
        for j in range(lo, hi):
            if a[j] <= pivot:
                i += 1
                a[i], a[j] = a[j], a[i]
        a[i + 1], a[hi] = a[hi], a[i + 1]
        p = i + 1
        qsort(a, lo, p - 1)
        qsort(a, p + 1, hi)


n = int(os.environ["BENCH_SCALE"])
arr = []
seed = 1
for i in range(n):
    seed = (seed * 1103515245 + 12345) % 2147483648
    arr.append(seed % 100)
t0 = time.perf_counter_ns()
qsort(arr, 0, n - 1)
t1 = time.perf_counter_ns()
print("qsort: a0 =", arr[0], "amid =", arr[n // 2], "alast =", arr[n - 1])
print("__bench_ns:", t1 - t0)

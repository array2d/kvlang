import os
import time


def bsearch(arr, n, target):
    lo, hi = 0, n - 1
    idx = -1
    while lo <= hi:
        mid = (lo + hi) // 2
        mv = arr[mid]
        if mv == target:
            idx = mid
            lo = hi + 1
        elif mv < target:
            lo = mid + 1
        else:
            hi = mid - 1
    return idx


n = int(os.environ["BENCH_SCALE"])
arr = [i for i in range(n)]
t0 = time.perf_counter_ns()
total = 0
found = 0
for q in range(n):
    idx = bsearch(arr, n, q)
    if idx != -1:
        found += 1
        total += idx
t1 = time.perf_counter_ns()
print("bsearch: found =", found, "sum =", total)
print("__bench_ns:", t1 - t0)

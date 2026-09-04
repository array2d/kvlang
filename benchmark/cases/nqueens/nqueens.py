import os
import time


def nq(cols, d1, d2, all):
    if cols == all:
        return 1
    cnt = 0
    avail = ((cols | d1 | d2) & all) ^ all
    while avail != 0:
        p = avail & (-avail)
        avail ^= p
        cnt += nq(cols | p, (d1 | p) << 1, (d2 | p) >> 1, all)
    return cnt


t0 = time.perf_counter_ns()
all = (1 << int(os.environ["BENCH_SCALE"])) - 1
ans = nq(0, 0, 0, all)
t1 = time.perf_counter_ns()
print("queens =", ans)
print("__bench_ns:", t1 - t0)

import os
import time


class Node:
    __slots__ = ("l", "r")

    def __init__(self, l, r):
        self.l = l
        self.r = r


def make(d):
    if d == 0:
        return Node(None, None)
    return Node(make(d - 1), make(d - 1))


def check(n):
    if n.l is None:
        return 1
    return 1 + check(n.l) + check(n.r)


depth = int(os.environ["BENCH_SCALE"])
t0 = time.perf_counter_ns()
root = make(depth)
count = check(root)
t1 = time.perf_counter_ns()
print("bintree: nodes =", count)
print("__bench_ns:", t1 - t0)

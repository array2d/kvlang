#!/usr/bin/env python3
"""numpy 扩展验证：ndarray buffer 建立在 kvspace-c SHM 上（零拷贝）。

验证点：
  1. tensor_alloc 写 float64 数组到 kvspace；
  2. tensor_view 返回的 ndarray 直接指向 SHM 的 raw data；
  3. 原地改 ndarray 后，从 kvspace 重新 view 能看到改动（证明 buffer 即 kvspace）。
"""

import numpy as np

from numpy_ext import NumpyExt


def main():
    dsn = "shm:///tmp/numpy_test"
    with NumpyExt(dsn) as kv:
        # 1. 分配两个 float64 数组
        a = np.array([1.0, 2.0, 3.0], dtype=np.float64)
        b = np.array([4.0, 5.0, 6.0], dtype=np.float64)
        kv.tensor_alloc("/t/a", a)
        kv.tensor_alloc("/t/b", b)

        # 2. 零拷贝视图
        va = kv.tensor_view("/t/a")
        vb = kv.tensor_view("/t/b")
        assert va is not None and vb is not None
        assert list(va) == [1.0, 2.0, 3.0], f"a = {list(va)}"
        assert list(vb) == [4.0, 5.0, 6.0], f"b = {list(vb)}"
        print(f"a = {list(va)}")
        print(f"b = {list(vb)}")

        # 3. 原地改 ndarray（直接写 SHM buffer）
        va[0] = 100.0
        va[2] = 300.0

        # 4. 重新 view 读回，证明改动落在 kvspace
        va2 = kv.tensor_view("/t/a")
        assert list(va2) == [100.0, 2.0, 300.0], f"a2 = {list(va2)}"
        print(f"a (改后) = {list(va2)}  ← 零拷贝写回 kvspace 生效")

        # 5. numpy 计算 c = a + b，写回 kvspace
        c = va2 + vb
        kv.tensor_alloc("/t/c", c)
        vc = kv.tensor_view("/t/c")
        assert list(vc) == [104.0, 7.0, 306.0], f"c = {list(vc)}"
        print(f"c = a + b = {list(vc)}")

        # 6. int64 数组同样支持
        iv = np.array([10, 20, 30], dtype=np.int64)
        kv.tensor_alloc("/t/i", iv)
        vi = kv.tensor_view("/t/i")
        assert list(vi) == [10, 20, 30]
        print(f"i = {list(vi)}")

    print("PASS: ndarray buffer 建立在 kvspace-c SHM 上")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""可选一致性校验：本地 DB 导出（CSV）存在时，核对其 impl 列与仓库规则一致。

仓库（native 清单 + gen_ops.py 规则）是权威；DB 只是分析副本。
CSV 缺 impl 列或文件不存在 → 跳过（返回 0），不影响 CI。
"""
import csv
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_gen():
    spec = importlib.util.spec_from_file_location("gen_ops", os.path.join(HERE, "gen_ops.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    # 权威：仓库留档 DB（workspace/py-lua-bytecode-ref/opcodes-499.csv）。
    # impl 列由 gen_ops.py 规则生成，DB 只作分析视图；缺列/文件不存在 → SKIP。
    csv_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, "../../workspace/py-lua-bytecode-ref/opcodes-499.csv")
    if not os.path.isfile(csv_path):
        print("SKIP: no DB export")
        return 0
    gen = load_gen()
    rows = list(csv.DictReader(open(csv_path, encoding="utf-8")))
    if not rows or "impl" not in rows[0]:
        print("SKIP: DB export has no impl column yet")
        return 0
    bad = []
    for r in rows:
        name = r["name"]
        cls, op, kind, w, impl = gen.classify(name)
        want = impl.split("::", 1)[1]
        got = (r.get("impl") or "").strip()
        if got and got != want:
            bad.append((name, got, want))
    if bad:
        print(f"FAIL: impl mismatch for {len(bad)} ops")
        for n, got, want in bad[:10]:
            print(f"  {n}: db={got} rules={want}")
        return 1
    print(f"OK impl consistency ({len(rows)} ops)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

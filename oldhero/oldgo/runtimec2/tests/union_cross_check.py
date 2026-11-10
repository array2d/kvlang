#!/usr/bin/env python3
"""阶段 A 交叉校验驱动：tutorial（有期望输出、非 extern/wip）+ benchmark 10。

每个用例：fresh shm → stdlib boot → layout → 解析 rwfunc → union_cross_check
逐函数比对 layout typed 行与 union 选出的实现。

判定（三条同时成立才通过）：
1. 所有用例的 mismatched 合计为 0；
2. 布局失败（layout_failed）集合 ⊆ 已登记基线
   `union_layout_failed_baseline.txt`——不在基线里的布局失败一律判失败，
   不给新的布局回归留静默放行口子；
3. 其它失败（超时 / 工具无 summary）为 0。
基线里已被修复的条目会打印提醒，可从基线移除。
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


def has_expects(path):
    with open(path, encoding="utf-8") as f:
        return any(line.startswith("// 期望输出") for line in f)


def needs_skip(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("// extern") or line.startswith("// wip"):
                return True
            if line.strip() and not line.startswith("//"):
                return False
    return False


def functions(path):
    """解析 rwfunc 及其 pkg（lib 块），返回 /lib/... 函数目录列表。"""
    out, pkg, depth, lib_depth = [], "", 0, None
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("//")[0].rstrip()
            m = re.match(r"\s*lib\s+([A-Za-z_][\w/·]*)\s*\{", line)
            if m:
                pkg = m.group(1)
                lib_depth = depth
            for ch in line:
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if lib_depth is not None and depth <= lib_depth:
                        pkg, lib_depth = "", None
            m = re.match(r"\s*rwfunc\s+([A-Za-z_][\w]*)\s*\(", line)
            if not m:
                continue
            name = m.group(1)
            out.append("/lib/" + (pkg + "·" + name if pkg else name))
    return out


def sweep_scale(root, case):
    import importlib.util
    path = os.path.join(root, "benchmark/run.py")
    spec = importlib.util.spec_from_file_location("_bench_run", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.SWEEP.get(case, [1])[0]


def run_case(tool, byteseek, layout, root, source, work):
    dsn = "shm://" + os.path.join(work, "store.shm")
    env = dict(os.environ, KVSPACE=dsn)
    subprocess.run([byteseek, "-c", 'print("boot")'], env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=120)
    r = subprocess.run([layout, source, dsn], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=120)
    if r.returncode != 0:
        return None, "layout failed"
    funcs = functions(source)
    if not funcs:
        return None, "no rwfunc"
    r = subprocess.run([tool, dsn, *funcs], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       text=True, timeout=300)
    return r, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--byteseek", required=True)
    ap.add_argument("--layout", required=True)
    ap.add_argument("--source-root", required=True)
    ap.add_argument("--filter", default="")
    ap.add_argument(
        "--baseline",
        default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "union_layout_failed_baseline.txt"),
        help="已登记的 layout 失败基线（每行一个用例标签，# 开头为注释）",
    )
    args = ap.parse_args()
    root = os.path.abspath(args.source_root)
    baseline = set()
    with open(args.baseline, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                baseline.add(line)

    cases = []
    for dirpath, _, names in os.walk(os.path.join(root, "tutorial")):
        for name in sorted(names):
            if not name.endswith(".kv"):
                continue
            path = os.path.join(dirpath, name)
            if not has_expects(path) or needs_skip(path):
                continue
            cases.append((os.path.relpath(path, root), path, None))
    for case in sorted(os.listdir(os.path.join(root, "benchmark/cases"))):
        src = os.path.join(root, "benchmark/cases", case, case + ".kv")
        if not os.path.isfile(src):
            continue
        cases.append(("bench-" + case, src, case))
    if args.filter:
        cases = [c for c in cases if args.filter in c[0]]

    total = {"cases": 0, "checked": 0, "matched": 0, "mismatched": 0,
             "undecidable": 0, "skipped": 0, "failed": 0, "layout_failed": 0,
             "no_typed_variant": 0, "und_const": 0, "und_member": 0,
             "und_pointer": 0, "und_untyped": 0}
    layout_failed = []
    failures = []
    work = tempfile.mkdtemp(prefix="unionx.")
    try:
        for label, path, case in cases:
            total["cases"] += 1
            source = path
            if case is not None:
                scale = sweep_scale(root, case)
                rendered = os.path.join(work, case + ".kv")
                with open(path, encoding="utf-8") as f:
                    text = f.read().replace("__SCALE__", str(scale))
                with open(rendered, "w", encoding="utf-8") as f:
                    f.write(text)
                source = rendered
            shutil.rmtree(os.path.join(work, "store.shm"), ignore_errors=True)
            try:
                r, err = run_case(args.tool, args.byteseek, args.layout, root,
                                  source, work)
            except subprocess.TimeoutExpired:
                r, err = None, "timeout"
            if r is None:
                if err == "layout failed":
                    # 布局失败单独成类：只在基线集合内允许（见文件头注释）。
                    total["layout_failed"] += 1
                    layout_failed.append(label)
                else:
                    total["failed"] += 1
                    failures.append((label, err))
                continue
            m = re.search(
                r"summary: checked=(\d+) matched=(\d+) mismatched=(\d+) "
                r"undecidable=(\d+) skipped=(\d+) no_typed_variant=(\d+) "
                r"und_const=(\d+) und_member=(\d+) und_pointer=(\d+) "
                r"und_untyped=(\d+)", r.stdout)
            if not m:
                total["failed"] += 1
                failures.append((label, "tool produced no summary"))
                continue
            (checked, matched, mismatched, undecidable, skipped,
             no_variant, und_const, und_member, und_pointer,
             und_untyped) = (int(x) for x in m.groups())
            total["checked"] += checked
            total["matched"] += matched
            total["mismatched"] += mismatched
            total["undecidable"] += undecidable
            total["no_typed_variant"] += no_variant
            total["und_const"] += und_const
            total["und_member"] += und_member
            total["und_pointer"] += und_pointer
            total["und_untyped"] += und_untyped
            total["skipped"] += skipped
            if r.returncode != 0 or mismatched != 0:
                failures.append((label, "mismatched=%d" % mismatched))
                for line in r.stdout.splitlines():
                    if line.startswith("  "):
                        print("%s: %s" % (label, line.strip()))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    # 判定：mismatched 必须为 0；layout 失败集合必须 ⊆ 基线（不在基线里的
    # 一律算失败，不给新的布局回归留静默放行口子）。
    extra_layout = sorted(set(layout_failed) - baseline)
    missing_layout = sorted(baseline - set(layout_failed))
    for label in extra_layout:
        failures.append((label, "layout failed (not in baseline)"))
    if missing_layout:
        head = ", ".join(missing_layout[:5])
        more = "" if len(missing_layout) <= 5 else " …(+%d)" % (len(missing_layout) - 5)
        print("baseline-only layout failures (已修复，可从基线移除): %s%s" %
              (head, more))

    print("union cross-check: cases=%d checked=%d matched=%d mismatched=%d "
          "undecidable=%d skipped=%d failed=%d layout_failed=%d "
          "baseline_missing=%d no_typed_variant=%d "
          "und_const=%d und_member=%d und_pointer=%d und_untyped=%d" %
          (total["cases"], total["checked"], total["matched"],
           total["mismatched"], total["undecidable"], total["skipped"],
           total["failed"], total["layout_failed"], len(missing_layout),
           total["no_typed_variant"], total["und_const"],
           total["und_member"], total["und_pointer"], total["und_untyped"]))
    for label, why in failures:
        print("FAIL %s: %s" % (label, why))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())

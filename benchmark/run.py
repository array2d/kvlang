#!/usr/bin/env python3
"""kvlang 跨语言 / 跨后端性能基准。

同一算法四份逻辑等价实现（cases/<name>/<name>.{kv,py,rs,c}）。
kvlang 是被测对象，分别在三个 kvspace 后端上跑，各占一列：
    kvlang_shm    —— kvspace-c 共享内存（shm://，内存态地板）
    kvlang_fs     —— kvspace-durable 文件后端（fs://）
    kvlang_redis  —— kvspace-durable redis 后端（redis://）
python / rust / c 与后端无关，作原生/脚本基线各一列。

设计目标：跨版本长期可比。
- 例子规模写死在各实现里，勿随版本改动；每份实现打印 `__bench_input: <规模>`
  （仅 kvlang 那份被采纳，与语言无关），落 csv 的 input 列。
- 计时约定稳定：每份实现打印一行 `__bench_ns: <整数纳秒>`，功能输出在前。
- 每次运行落一个独立快照文件 results/results-<tag>-<UTC 时间戳>.csv，
  文件名带 kvlang 版本与运行时刻；行内另记 python/rust/c 工具链版本。

用法：
    python3 benchmark/run.py                 # 全部 case，min of 3，写快照
    python3 benchmark/run.py -k prime_sieve  # 只跑某 case
    python3 benchmark/run.py --repeat 1      # 快跑（不取 min）
    python3 benchmark/run.py --backends shm,fs   # 只跑部分后端
    python3 benchmark/run.py --no-write      # 只打印不落 csv
    python3 benchmark/run.py --show          # 汇总 results/ 历史，不跑
"""
import argparse
import csv
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent
KVLANG_ROOT = ROOT.parent
CASES_DIR = ROOT / "cases"
RESULTS_DIR = ROOT / "results"
BENCH_RE = re.compile(r"__bench_ns:\s*(\d+)")
INPUT_RE = re.compile(r"__bench_input:\s*(.+)")
KV_BACKENDS = ("shm", "fs", "redis")  # kvlang 三后端列顺序
NATIVE = ("python", "rust", "c")
EXT = {"kvlang": ".kv", "python": ".py", "rust": ".rs", "c": ".c"}

# 输入规模阶梯：每个 case 在这一串规模上各跑一遍，逐点落一行 csv。
# 规模点亦属冻结契约——一经确定勿改，跨版本才可按同一规模对比。
# scale 传入方式：kvlang 走源码 __SCALE__ 占位替换；python/rust/c 走环境变量 BENCH_SCALE。
# 上限刻意放低，确保三后端全量约 1 小时内跑完（v0.2.5 每操作一次 KV 往返、常数因子仍大，
# 后续版本会持续优化、逐步逼近 python，这套快照正是用来逐版本追踪那条加速曲线的）。
SWEEP = {
    "iops":          [500, 1000, 2000],
    "prime_sieve":   [50, 75, 100],
    "fib":           [8, 9, 10],
    "nqueens":       [4, 5, 6],
    "quicksort":     [32, 64, 128],
    "binary_search": [16, 32, 64],
    "binary_trees":  [4, 5, 6],
    "hash_table":    [50, 100, 200],
    "matmul":        [4, 6, 8],
    "k_nucleotide":  [3, 5, 8],
}
SCALE_ENV = "BENCH_SCALE"   # python/rust/c 从此环境变量读规模
SCALE_TOKEN = "__SCALE__"   # kvlang 源码里的规模占位符
FIELDS = ["timestamp", "version", "commit", "cpu", "case", "input", "samples",
          "kvlang_shm_ns", "kvlang_fs_ns", "kvlang_redis_ns",
          "python_ns", "rust_ns", "c_ns",
          "python_ver", "rust_ver", "c_ver", "native_opt", "valid"]


def sh(cmd, timeout=None, env=None):
    return subprocess.run(cmd, capture_output=True, text=True,
                          timeout=timeout, env=env)


def git_version():
    v = sh(["git", "-C", str(KVLANG_ROOT), "describe", "--tags", "--always",
            "--dirty"]).stdout.strip() or "unknown"
    c = sh(["git", "-C", str(KVLANG_ROOT), "rev-parse", "--short",
            "HEAD"]).stdout.strip() or "unknown"
    return v, c


def git_tag():
    return sh(["git", "-C", str(KVLANG_ROOT), "describe", "--tags",
               "--abbrev=0"]).stdout.strip() or "v0"


def tool_versions():
    """python / rust / c 工具链版本，随 csv 落盘——基线也会随工具升级而变。"""
    py = platform.python_version()
    rm = re.search(r"rustc (\S+)", sh(["rustc", "--version"]).stdout)
    g = sh(["gcc", "--version"]).stdout.splitlines()
    gm = re.search(r"(\d+\.\d+\.\d+)", g[0]) if g else None
    return py, (rm.group(1) if rm else "unknown"), (gm.group(1) if gm else "unknown")


def cpu_model():
    """CPU 型号——跨机对比的前提。"""
    try:
        for ln in Path("/proc/cpuinfo").read_text().splitlines():
            if ln.startswith("model name"):
                return ln.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def parse(out):
    """(bench_ns|None, input|None, 功能输出) —— 剥掉两种标记行，其余为功能输出。"""
    ns, inp, func = None, None, []
    for ln in out.splitlines():
        m, mi = BENCH_RE.search(ln), INPUT_RE.search(ln)
        if m:
            ns = int(m.group(1))
        elif mi:
            inp = mi.group(1).strip()
        else:
            func.append(ln)
    return ns, inp, "\n".join(func).rstrip()


# 禁用 -O2/-O3 的过度优化：那些优化会把无外部副作用的循环闭式折叠/消除
# （iops 的 a+=1 在 gcc -O2 下 2000 次循环塌成常量，88ns 纯失真），
# -O1 保留真实工作量又不做激进变换。python 解释执行无编译期优化，不涉及。
NATIVE_OPT = "gcc -O1 / rustc opt-level=1 / cpython"


def compile_native(lang, src, workdir):
    exe = workdir / "bench.out"
    if lang == "rust":
        cc = sh(["rustc", "-C", "opt-level=1", "-o", str(exe), str(src)])
    else:
        cc = sh(["gcc", "-O1", "-o", str(exe), str(src)])
    if cc.returncode != 0:
        raise RuntimeError(f"{lang} 编译失败:\n{cc.stderr}")
    return [str(exe)]


def kv_dsn(label, workdir, redis_dsn):
    if label == "shm":
        return f"shm://{workdir}/bench.shm"
    if label == "fs":
        return f"fs://{workdir}/bench.fs"
    return redis_dsn


def kv_reset(label, workdir, redis_dsn):
    """每次采样前清空该后端，杜绝残留污染。"""
    if label == "shm":
        Path(f"{workdir}/bench.shm").unlink(missing_ok=True)
    elif label == "fs":
        shutil.rmtree(f"{workdir}/bench.fs", ignore_errors=True)
    else:
        addr = redis_dsn.split("://", 1)[-1]
        host, _, port = addr.partition(":")
        sh(["redis-cli", "-h", host or "127.0.0.1", "-p", port or "6379",
            "flushall"], timeout=10)


def run_kvlang(case_dir, label, args, workdir, scale):
    # kvlang 无从脚本读环境变量的 builtin，故按规模把源码里的 __SCALE__ 占位替换后落临时文件再跑。
    text = (case_dir / (case_dir.name + ".kv")).read_text()
    src = workdir / (case_dir.name + ".kv")
    src.write_text(text.replace(SCALE_TOKEN, str(scale)))
    dsn = kv_dsn(label, workdir, args.redis)

    def _run():
        kv_reset(label, workdir, args.redis)
        env = {**os.environ, "KVSPACE": dsn}
        return sh([args.kvlang_bin, str(src)], timeout=args.timeout,
                  env=env).stdout
    return _run


def run_native(lang, case_dir, workdir, scale):
    # python/rust/c 源码不含占位符，规模经环境变量 BENCH_SCALE 传入（编译与规模无关，只运行时读取）。
    src = case_dir / (case_dir.name + EXT[lang])
    if lang == "python":
        cmd = [sys.executable, str(src)]
    else:
        cmd = compile_native(lang, src, workdir)
    env = {**os.environ, SCALE_ENV: str(scale)}

    def _run():
        return sh(cmd, timeout=300, env=env).stdout
    return _run


def sample(runner, repeat, func_sink, key):
    """返回 (min_ns|None, input|None)。"""
    best, inp = None, None
    for _ in range(repeat):
        ns, i, f = parse(runner())
        func_sink[key] = f
        if i is not None:
            inp = i
        if ns is not None and (best is None or ns < best):
            best = ns
    return best, inp


def bench_case(case_dir, args, scale):
    best, func, case_input = {}, {}, None
    for label in args.backends:
        key = f"kvlang_{label}"
        with tempfile.TemporaryDirectory(prefix=f"bench-kv-{label}-") as tmp:
            try:
                b, inp = sample(
                    run_kvlang(case_dir, label, args, Path(tmp), scale),
                    args.repeat, func, key)
                best[key] = b
                if inp is not None:
                    case_input = inp
            except (subprocess.TimeoutExpired, RuntimeError) as e:
                print(f"  ! kvlang/{label} 失败: {str(e).splitlines()[0]}",
                      file=sys.stderr)
                best[key] = None
    for lang in NATIVE:
        if not (case_dir / (case_dir.name + EXT[lang])).exists():
            best[lang] = None
            continue
        with tempfile.TemporaryDirectory(prefix=f"bench-{lang}-") as tmp:
            try:
                b, _ = sample(run_native(lang, case_dir, Path(tmp), scale),
                              args.repeat, func, lang)
                best[lang] = b
            except (subprocess.TimeoutExpired, RuntimeError) as e:
                print(f"  ! {lang} 失败: {str(e).splitlines()[0]}",
                      file=sys.stderr)
                best[lang] = None
    present = [func[k] for k in func if best.get(k) is not None]
    valid = len(present) >= 2 and all(x == present[0] for x in present)
    return best, valid, case_input


def fmt_ns(ns):
    if ns is None:
        return "-"
    if ns >= 1e9:
        return f"{ns/1e9:.3f}s"
    if ns >= 1e6:
        return f"{ns/1e6:.3f}ms"
    if ns >= 1e3:
        return f"{ns/1e3:.3f}µs"
    return f"{ns}ns"


NS_HDR = ("kv-shm", "kv-fs", "kv-redis", "python", "rust", "c")
COLS = ("kvlang_shm", "kvlang_fs", "kvlang_redis", "python", "rust", "c")


def print_header():
    print(f"{'case':<14}{'input':<14}" + "".join(f"{h:>11}" for h in NS_HDR)
          + "  valid")


def print_row(name, inp, best, valid):
    cells = "".join(f"{fmt_ns(best.get(c)):>11}" for c in COLS)
    print(f"{name:<14}{(inp or '-'):<14}{cells}  {valid}")


def show_history():
    files = sorted(RESULTS_DIR.glob("results-*.csv"))
    if not files:
        print("无历史（results/ 无快照）")
        return
    rows = []
    for fp in files:
        with fp.open() as f:
            rows.extend(csv.DictReader(f))
    if not rows:
        print("results/ 快照为空")
        return
    w = max(len(r["version"]) for r in rows) + 1
    iw = max([len(r.get("input", "") or "-") for r in rows] + [5]) + 1
    print(f"{'version':<{w}}{'case':<14}{'input':<{iw}}"
          + "".join(f"{h:>11}" for h in NS_HDR) + "  valid")
    for r in rows:
        def g(c):
            v = r.get(c + "_ns", "")
            return fmt_ns(int(v)) if v not in ("", "-") else "-"
        cells = "".join(f"{g(c):>11}" for c in COLS)
        print(f"{r['version']:<{w}}{r['case']:<14}"
              f"{(r.get('input') or '-'):<{iw}}{cells}  {r['valid']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", "--filter", default="", help="只跑名字含此串的 case")
    ap.add_argument("--repeat", type=int, default=3, help="每列采样次数，取 min")
    ap.add_argument("--backends", default="shm,fs,redis",
                    help="kvlang 后端子集，逗号分隔（shm,fs,redis）")
    ap.add_argument("--redis", default="redis://127.0.0.1:6379",
                    help="redis 后端 DSN")
    ap.add_argument("--kvlang-bin", default="/usr/bin/kvlang")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--no-write", action="store_true", help="不写快照")
    ap.add_argument("--show", action="store_true", help="汇总历史后退出")
    args = ap.parse_args()

    if args.show:
        show_history()
        return

    args.backends = [b for b in args.backends.split(",") if b in KV_BACKENDS]
    if not args.backends:
        print("无有效后端", file=sys.stderr)
        sys.exit(1)

    version, commit = git_version()
    py_ver, rust_ver, c_ver = tool_versions()
    cpu = cpu_model()
    now = datetime.now(timezone.utc)
    ts = now.strftime("%Y-%m-%dT%H:%M:%SZ")
    cases = sorted(d for d in CASES_DIR.iterdir()
                   if d.is_dir() and args.filter in d.name)
    if not cases:
        print("无匹配 case", file=sys.stderr)
        sys.exit(1)

    print(f"version={version} commit={commit} cpu=[{cpu}]\n"
          f"python={py_ver} rust={rust_ver} gcc={c_ver} opt=[{NATIVE_OPT}] "
          f"backends={','.join(args.backends)} repeat={args.repeat}\n")
    print_header()
    rows = []
    for cd in cases:
        scales = SWEEP.get(cd.name, [1])
        for scale in scales:
            best, valid, cinp = bench_case(cd, args, scale)
            cinp = cinp or str(scale)
            print_row(cd.name, cinp, best, valid)
            rows.append({
                "timestamp": ts, "version": version, "commit": commit,
                "cpu": cpu, "case": cd.name, "input": cinp,
                "samples": args.repeat,
                "kvlang_shm_ns": best.get("kvlang_shm") or "",
                "kvlang_fs_ns": best.get("kvlang_fs") or "",
                "kvlang_redis_ns": best.get("kvlang_redis") or "",
                "python_ns": best.get("python") or "",
                "rust_ns": best.get("rust") or "",
                "c_ns": best.get("c") or "",
                "python_ver": py_ver, "rust_ver": rust_ver, "c_ver": c_ver,
                "native_opt": NATIVE_OPT, "valid": valid,
            })

    if args.no_write:
        return
    RESULTS_DIR.mkdir(exist_ok=True)
    fname = f"results-{git_tag()}-{now.strftime('%Y%m%dT%H%M%SZ')}.csv"
    out_path = RESULTS_DIR / fname
    with out_path.open("w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=FIELDS)
        wr.writeheader()
        wr.writerows(rows)
    print(f"\n→ 写入 {len(rows)} 行到 {out_path.relative_to(KVLANG_ROOT)}")


if __name__ == "__main__":
    main()

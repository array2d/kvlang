#!/usr/bin/env python3
"""if/while/for lower to goto/br; execute changes irseq, does not open a scope frame.

  KVSPACE_BACKEND_PATH=.deps/lib/kvspace \\
  LD_LIBRARY_PATH=bin:.deps/lib:.deps/lib/kvspace \\
  python3 tutorial/issue116_exp.py
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYOUT = os.environ.get("KVLANG_LAYOUT_BIN", str(ROOT / "bin" / "kvlanglayout"))
TERM = os.environ.get("KVLANG_TERM_BIN", str(ROOT / "bin" / "kvlang"))
PC_FLAT = re.compile(r"^/vthread/[0-9]+/\[[0-9]+\]/\[[0-9]+,-?[0-9]+\]$")
SCOPE_MARK = re.compile(
    r"_(then|else|while|do|if|merge|exit|for_init|for_cond|for_body|for_exit)_\d+"
)


def env_for(dsn: str) -> dict[str, str]:
    e = dict(os.environ)
    e["KVSPACE"] = dsn
    deps = ROOT / ".deps"
    lib = str(ROOT / "bin")
    extra = [lib, str(deps / "lib"), str(deps / "lib" / "kvspace")]
    e["LD_LIBRARY_PATH"] = ":".join(extra + [e.get("LD_LIBRARY_PATH", "")])
    e["LIBRARY_PATH"] = ":".join([str(deps / "lib"), e.get("LIBRARY_PATH", "")])
    e["KVSPACE_BACKEND_PATH"] = str(deps / "lib" / "kvspace")
    return e


def tlv_char(data: bytes) -> str:
    # head: 1B kindexpr_len, kindexpr, padding... we only need trailing UTF-8 after last NUL run.
    # fs dump of char/utf8: b'\x0e[20]char/utf8\x00...\x14\x00\x00\x00/vthread/...'
    if b"char/utf8" in data:
        i = data.find(b"char/utf8")
        rest = data[i + len(b"char/utf8") :]
        # skip NULs and 4B body_len
        rest = rest.lstrip(b"\x00")
        if len(rest) >= 4:
            n = int.from_bytes(rest[:4], "little")
            return rest[4 : 4 + n].decode("utf-8", "replace")
        return rest.decode("utf-8", "replace").strip("\x00")
    return data.decode("utf-8", "replace")


def store_files(store: Path) -> list[Path]:
    return [p for p in store.rglob("*") if p.is_file()]


def rel(store: Path, p: Path) -> str:
    return "/" + str(p.relative_to(store))


def all_rels(store: Path) -> list[str]:
    return [rel(store, p) for p in store.rglob("*")]


def read_pc(store: Path, vtid: str = "1") -> str:
    p = store / "vthread" / vtid / "‥pc"
    if not p.is_file():
        vt = store / "vthread" / vtid
        if vt.is_dir():
            for cand in vt.iterdir():
                if cand.name.endswith("pc") and "pc" in cand.name:
                    p = cand
                    break
    if not p.is_file():
        return ""
    return tlv_char(p.read_bytes()).strip()


def frame_dirs(store: Path, vtid: str = "1") -> list[str]:
    vt = store / "vthread" / vtid
    out = []
    if not vt.is_dir():
        return out
    for c in vt.iterdir():
        name = c.name
        if c.is_dir() and name.startswith("[") and name.endswith("]") and "," not in name:
            out.append(name)
    return sorted(out, key=lambda s: int(s[1:-1]))


def vthread_rels(store: Path) -> list[str]:
    return [r for r in all_rels(store) if r.startswith("/vthread/")]


def scope_hits(paths: list[str]) -> list[str]:
    return [p for p in paths if SCOPE_MARK.search(p)]


def layout_and_run(src: str, store: Path, timeout: int = 30) -> subprocess.CompletedProcess[str]:
    if store.exists():
        shutil.rmtree(store)
    store.mkdir(parents=True)
    dsn = f"fs://{store}"
    kvsrc = store / "prog.kv"
    kvsrc.write_text(src, encoding="utf-8")
    e = env_for(dsn)
    lay = subprocess.run([LAYOUT, str(kvsrc), dsn], capture_output=True, text=True, cwd=str(ROOT), timeout=timeout, env=e)
    if lay.returncode != 0:
        raise RuntimeError(f"layout failed: {lay.stderr}\n{lay.stdout}")
    return subprocess.run([TERM, "init"], capture_output=True, text=True, cwd=str(ROOT), timeout=timeout, env=e)


def assert_pc_flat(pc: str, ctx: str) -> None:
    if not PC_FLAT.match(pc):
        raise AssertionError(f"{ctx}: PC not flat: {pc!r}")
    if pc.count("[") != 2:
        raise AssertionError(f"{ctx}: PC should have 2 coord segments: {pc!r}")
    if SCOPE_MARK.search(pc):
        raise AssertionError(f"{ctx}: PC still has scope segment: {pc!r}")


def assert_no_vthread_scope(store: Path, ctx: str) -> None:
    hits = scope_hits(vthread_rels(store))
    if hits:
        raise AssertionError(f"{ctx}: vthread still has scope keys: {hits[:12]}")


def assert_frames(store: Path, ctx: str, n: int) -> list[str]:
    frames = frame_dirs(store)
    if len(frames) != n:
        raise AssertionError(f"{ctx}: extra frames {frames} want {n} pc={read_pc(store)}")
    return frames


def run_ok(src: str, expect: str, ctx: str) -> str:
    store = Path(tempfile.mkdtemp(prefix=f"i116-{ctx}-"))
    try:
        r = layout_and_run(src, store)
        out = (r.stdout or "") + (r.stderr or "")
        if r.returncode != 0:
            raise AssertionError(f"{ctx}: exit {r.returncode} out={out[:400]}")
        if expect not in r.stdout:
            raise AssertionError(f"{ctx}: missing {expect!r} in stdout={r.stdout!r} stderr={r.stderr[:200]!r}")
        assert_no_vthread_scope(store, ctx)
        lib_hits = scope_hits([p for p in all_rels(store) if p.startswith("/lib/") and "/[" in p])
        if lib_hits:
            raise AssertionError(f"{ctx}: lib still has scope-flat keys: {lib_hits[:8]}")
        print(f"  {ctx} stdout={r.stdout.strip()!r} frames={frame_dirs(store)}")
        return r.stdout
    finally:
        shutil.rmtree(store, ignore_errors=True)


def run_crash(src: str, ctx: str, want_frames: int) -> str:
    store = Path(tempfile.mkdtemp(prefix=f"i116-{ctx}-"))
    try:
        r = layout_and_run(src, store)
        pc = read_pc(store)
        assert_pc_flat(pc, ctx)
        assert_no_vthread_scope(store, ctx)
        frames = assert_frames(store, ctx, want_frames)
        n = int(re.search(r"/\[(\d+)\]/", pc).group(1))
        if n != want_frames:
            raise AssertionError(f"{ctx}: FrameNum={n} pc={pc} want {want_frames}")
        print(f"  {ctx} pc={pc} frames={frames} err={(r.stderr or '')[:80]!r}")
        return pc
    finally:
        shutil.rmtree(store, ignore_errors=True)


def exp_bootstrap_depth1() -> None:
    store = Path(tempfile.mkdtemp(prefix="i116-boot-"))
    try:
        src = 'rwfunc main() -> () { 1 -> /ok }\nmain()\n'
        layout_and_run(src, store)
        pc = read_pc(store)
        # done: last PC is return slot on frame [1] or [2]
        frames = frame_dirs(store)
        # after return of depth-1, frame may be deleted; bootstrap created [1]
        keys = [rel(store, p) for p in store_files(store)]
        if any("/vthread/" in k and "_else" in k for k in keys):
            raise AssertionError(f"scope path under vthread: {keys}")
        print(f"  bootstrap pc={pc!r} frames={frames} keys={len(keys)}")
        if pc:
            assert_pc_flat(pc, "bootstrap")
    finally:
        shutil.rmtree(store, ignore_errors=True)


def exp_while_same_frame() -> None:
    store = Path(tempfile.mkdtemp(prefix="i116-while-"))
    try:
        src = """rwfunc main() -> () {
    1 -> i
    while (i <= 3) {
        i <- i + 1
    }
    1 ÷ 0 -> x
}
main()
"""
        r = layout_and_run(src, store)
        pc = read_pc(store)
        assert_pc_flat(pc, "while")
        n = int(re.search(r"/\[(\d+)\]/", pc).group(1))
        if n != 2:
            raise AssertionError(f"while FrameNum={n} pc={pc} want 2 (init+main)")
        frames = frame_dirs(store)
        if len(frames) != 2:
            raise AssertionError(f"while extra frames: {frames} pc={pc}")
        print(f"  while pc={pc} frames={frames} stderr={r.stderr.strip()[:80]!r}")
    finally:
        shutil.rmtree(store, ignore_errors=True)


def exp_else_recursion_flat() -> None:
    store = Path(tempfile.mkdtemp(prefix="i116-rec-"))
    try:
        src = """rwfunc rec(n:int64) -> (r:int64) {
    if (n <= 0) {
        1 ÷ 0 -> r
    } else {
        n - 1 -> n1
        rec(n1) -> r
    }
}
rec(3)
"""
        layout_and_run(src, store)
        pc = read_pc(store)
        assert_pc_flat(pc, "else-rec")
        n = int(re.search(r"/\[(\d+)\]/", pc).group(1))
        if n != 5:
            raise AssertionError(f"else-rec FrameNum={n} pc={pc} want 5 (init+rec×4)")
        print(f"  else-rec pc={pc} FrameNum={n} len={len(pc)}")
    finally:
        shutil.rmtree(store, ignore_errors=True)


def exp_pc_len_vs_depth() -> None:
    """PC byte length must stay ~constant as depth grows (not +14B/level)."""
    rows = []
    for depth in (3, 8, 15):
        store = Path(tempfile.mkdtemp(prefix=f"i116-d{depth}-"))
        try:
            src = f"""rwfunc rec(n:int64) -> (r:int64) {{
    if (n <= 0) {{
        1 ÷ 0 -> r
    }} else {{
        n - 1 -> n1
        rec(n1) -> r
    }}
}}
rec({depth})
"""
            layout_and_run(src, store)
            pc = read_pc(store)
            assert_pc_flat(pc, f"depth={depth}")
            n = int(re.search(r"/\[(\d+)\]/", pc).group(1))
            rows.append((depth, n, len(pc), pc))
        finally:
            shutil.rmtree(store, ignore_errors=True)
    print("  depth  FrameNum  PClen  PC")
    for depth, n, L, pc in rows:
        print(f"  {depth:5d}  {n:8d}  {L:5d}  {pc}")
    lengths = [L for _, _, L, _ in rows]
    # old model PC(d) ≈ 22+14(d-1); d=15 → ~218. new: ~23–26.
    if max(lengths) - min(lengths) > 4:
        raise AssertionError(f"PC length grew too much across depths: {rows}")
    if max(lengths) > 40:
        raise AssertionError(f"PC still O(D)? {rows}")


def exp_stack_overflow_flat() -> None:
    store = Path(tempfile.mkdtemp(prefix="i116-ovf-"))
    try:
        src = Path(ROOT / "tutorial/error_cases/recursion_error/stack_overflow.kv").read_text()
        r = layout_and_run(src, store)
        pc = read_pc(store)
        assert_pc_flat(pc, "overflow")
        n = int(re.search(r"/\[(\d+)\]/", pc).group(1))
        if n != 257:
            raise AssertionError(f"overflow FrameNum={n} pc={pc} want 257 (MaxStackDepth+1)")
        err = r.stderr + r.stdout
        if "RecursionError" not in err and "stack overflow" not in err:
            raise AssertionError(f"overflow missing RecursionError: {err[:300]}")
        print(f"  overflow pc={pc} FrameNum={n} len={len(pc)}")
    finally:
        shutil.rmtree(store, ignore_errors=True)


def exp_lib_no_scope_keys() -> None:
    store = Path(tempfile.mkdtemp(prefix="i116-lib-"))
    try:
        src = Path(ROOT / "tutorial/03-control/while.kv").read_text()
        layout_and_run(src, store)
        bad = []
        for p in store_files(store):
            r = rel(store, p)
            if "/lib/" in r and re.search(r"/_[A-Za-z]+_[0-9]+\[", r):
                bad.append(r)
        if bad:
            raise AssertionError(f"lib still has scope keys: {bad[:10]}")
        print(f"  lib plane clean, files={len(store_files(store))}")
    finally:
        shutil.rmtree(store, ignore_errors=True)


def exp_if_then_result() -> None:
    src = """rwfunc main() -> () {
    0 - 5 -> x
    if (x < 0) {
        0 - x -> x
    } else {
        x -> x
    }
    println(x)
}
main()
"""
    run_ok(src, "5", "if-then")


def exp_if_else_result() -> None:
    src = """rwfunc main() -> () {
    3 -> x
    if (x < 0) {
        0 - x -> x
    } else {
        x + 1 -> x
    }
    println(x)
}
main()
"""
    run_ok(src, "4", "if-else")


def exp_if_crash_same_frame() -> None:
    run_crash(
        """rwfunc main() -> () {
    if (1 < 2) {
        1 ÷ 0 -> x
    } else {
        0 -> x
    }
}
main()
""",
        "if-then-crash",
        2,
    )
    run_crash(
        """rwfunc main() -> () {
    if (2 < 1) {
        0 -> x
    } else {
        1 ÷ 0 -> x
    }
}
main()
""",
        "if-else-crash",
        2,
    )


def exp_while_result() -> None:
    src = """rwfunc main() -> () {
    0 -> acc
    1 -> i
    while (i <= 10) {
        acc + i -> acc
        i <- i + 1
    }
    println(acc)
}
main()
"""
    run_ok(src, "55", "while-sum")


def exp_break_result() -> None:
    src = """rwfunc main() -> () {
    0 -> i
    while (i < 100) {
        if (i == 5) {
            break
        }
        i <- i + 1
    }
    println(i)
}
main()
"""
    run_ok(src, "5", "break")


def exp_continue_result() -> None:
    src = """rwfunc main() -> () {
    0 -> acc
    1 -> i
    while (i <= 5) {
        if (i == 3) {
            i <- i + 1
            continue
        }
        acc <- acc + i
        i <- i + 1
    }
    println(acc)
}
main()
"""
    run_ok(src, "12", "continue")


def exp_nested_while_if_crash() -> None:
    run_crash(
        """rwfunc main() -> () {
    1 -> i
    while (i <= 2) {
        1 -> j
        while (j <= 2) {
            if (i == 2) {
                1 ÷ 0 -> x
            }
            j <- j + 1
        }
        i <- i + 1
    }
}
main()
""",
        "nest-while-if-crash",
        2,
    )


def parse_expects(text: str) -> list[str]:
    pats: list[str] = []
    in_block = False
    for line in text.splitlines():
        if line.startswith("# 期望输出"):
            in_block = True
            continue
        if in_block:
            if line.startswith("#   ") or line.startswith("# \t"):
                p = line[2:].strip()
                p = re.sub(r"\s*\(.*\)\s*$", "", p)
                if p:
                    pats.append(p)
            elif not line.startswith("#"):
                break
    return pats


def exp_tutorial_control() -> None:
    files = [
        ROOT / "tutorial/03-control/if.kv",
        ROOT / "tutorial/03-control/while.kv",
        ROOT / "tutorial/03-control/guess.kv",
        ROOT / "tutorial/03-control/classify.kv",
        ROOT / "tutorial/03-control/for.kv",
    ]
    for f in files:
        src = f.read_text(encoding="utf-8")
        expects = parse_expects(src)
        if not expects:
            raise AssertionError(f"{f.name}: no 期望输出")
        store = Path(tempfile.mkdtemp(prefix=f"i116-{f.stem}-"))
        try:
            r = layout_and_run(src, store, timeout=60)
            out = r.stdout or ""
            if r.returncode != 0:
                raise AssertionError(f"{f.name}: exit {r.returncode} {r.stderr[:300]}")
            missing = [e for e in expects if e not in out]
            if missing:
                raise AssertionError(f"{f.name}: missing {missing} stdout={out[:400]!r}")
            assert_no_vthread_scope(store, f.name)
            print(f"  tutorial {f.name} ok frames={frame_dirs(store)}")
        finally:
            shutil.rmtree(store, ignore_errors=True)


def exp_break_in_prime_sieve_layout_run() -> None:
    src = Path(ROOT / "tutorial/06-algo/prime_sieve.kv").read_text(encoding="utf-8")
    src = src.replace("prime_sieve(200)", "prime_sieve(30)")
    store = Path(tempfile.mkdtemp(prefix="i116-sieve-"))
    try:
        r = layout_and_run(src, store, timeout=60)
        out = r.stdout or ""
        if r.returncode != 0:
            raise AssertionError(f"sieve: exit {r.returncode} {r.stderr[:300]}")
        if "prime: 29" not in out:
            raise AssertionError(f"sieve missing prime 29: {out[:400]!r}")
        if "total primes up to 30 =" not in out:
            raise AssertionError(f"sieve missing total: {out[:400]!r}")
        assert_no_vthread_scope(store, "sieve")
        print(f"  sieve(30) ok")
    finally:
        shutil.rmtree(store, ignore_errors=True)


def main() -> int:
    for binp, name in ((LAYOUT, "layout"), (TERM, "term")):
        if not Path(binp).is_file():
            print(f"missing {name} binary: {binp}", file=sys.stderr)
            return 2
    tests = [
        ("lib-no-scope-keys", exp_lib_no_scope_keys),
        ("bootstrap-depth1", exp_bootstrap_depth1),
        ("while-same-frame", exp_while_same_frame),
        ("else-recursion-flat", exp_else_recursion_flat),
        ("pc-len-vs-depth", exp_pc_len_vs_depth),
        ("stack-overflow-flat", exp_stack_overflow_flat),
        ("if-then-result", exp_if_then_result),
        ("if-else-result", exp_if_else_result),
        ("if-crash-same-frame", exp_if_crash_same_frame),
        ("while-result", exp_while_result),
        ("break-result", exp_break_result),
        ("continue-result", exp_continue_result),
        ("nest-while-if-crash", exp_nested_while_if_crash),
        ("tutorial-control", exp_tutorial_control),
        ("break-sieve-30", exp_break_in_prime_sieve_layout_run),
    ]
    failed = 0
    for name, fn in tests:
        try:
            print(f"== {name}")
            fn()
            print(f"OK {name}")
        except Exception as e:
            failed += 1
            print(f"FAIL {name}: {e}")
    print(f"══ PASS:{len(tests) - failed}  FAIL:{failed} ══")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

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


def assert_no_vthread_scope(store: Path, ctx: str) -> None:
    hits = scope_hits(vthread_rels(store))
    if hits:
        raise AssertionError(f"{ctx}: vthread still has scope keys: {hits[:12]}")
    pc = read_pc(store)
    if pc and SCOPE_MARK.search(pc):
        raise AssertionError(f"{ctx}: PC still has scope segment: {pc!r}")


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
        print(f"  {ctx} stdout={r.stdout.strip()!r}")
        return r.stdout
    finally:
        shutil.rmtree(store, ignore_errors=True)


def run_crash(src: str, ctx: str) -> str:
    store = Path(tempfile.mkdtemp(prefix=f"i116-{ctx}-"))
    try:
        r = layout_and_run(src, store)
        pc = read_pc(store)
        assert_no_vthread_scope(store, ctx)
        print(f"  {ctx} pc={pc} err={(r.stderr or '')[:80]!r}")
        return pc
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
            print(f"  tutorial {f.name} ok")
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

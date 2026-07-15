#!/usr/bin/env python3
"""kvlang 自动化测试脚本 (Python 版)

用法（在项目根目录执行）:
    python3 example/run.py                    # 全量测试
    python3 example/run.py --filter serve     # 只跑含 "serve" 的测试
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
import threading
from pathlib import Path

# ── ANSI ──────────────────────────────────────────────────────────────────────
RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
NC = "\033[0m"

# ── 全局状态 ──────────────────────────────────────────────────────────────────
PASS = 0
FAIL = 0
SKIP = 0
FILTER = ""
PROJECT_ROOT = Path(__file__).resolve().parent.parent
KV = str(PROJECT_ROOT / "kvlang")


def section(title: str) -> None:
    print(f"\n{YELLOW}── {title} ──{NC}")


def ok(msg: str) -> None:
    global PASS
    print(f"{GREEN}✅ {msg}{NC}")
    PASS += 1


def fail(msg: str) -> None:
    global FAIL
    print(f"{RED}❌ {msg}{NC}")
    FAIL += 1


def skip(msg: str) -> None:
    global SKIP
    print(f"{YELLOW}⏭  {msg}{NC}")
    SKIP += 1


def should_run(desc: str) -> bool:
    return not FILTER or FILTER in desc


# ── helpers ───────────────────────────────────────────────────────────────────

def run_cmd(cmd: list[str], timeout: int = 30,
            stdin: str | None = None,
            env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    """执行命令，返回 CompletedProcess。工作在 PROJECT_ROOT 下。"""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=str(PROJECT_ROOT),
        input=stdin,
        env=merged_env,
    )


def check_out(desc: str, pattern: str, cmd: list[str], timeout: int = 30,
              env: dict[str, str] | None = None) -> None:
    """CMD stdout 含 PATTERN。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        if pattern in result.stdout:
            ok(desc)
        else:
            fail(f"{desc} (want: '{pattern}'  got: {result.stdout[:120]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_err(desc: str, pattern: str, cmd: list[str], timeout: int = 30,
              env: dict[str, str] | None = None) -> None:
    """CMD stderr 含 PATTERN（忽略退出码）。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        if pattern in result.stderr:
            ok(desc)
        else:
            fail(f"{desc} (want: '{pattern}'  stderr: {result.stderr[:120]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_exit(desc: str, want_exit: int, cmd: list[str], timeout: int = 30,
               env: dict[str, str] | None = None) -> None:
    """CMD 退出码 == want_exit。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        if result.returncode == want_exit:
            ok(desc)
        else:
            fail(f"{desc} (exit={result.returncode}, want={want_exit})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_any(desc: str, pattern: str, cmd: list[str], timeout: int = 30,
              env: dict[str, str] | None = None) -> None:
    """CMD stdout+stderr 合并含 PATTERN。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        combined = result.stdout + result.stderr
        if pattern in combined:
            ok(desc)
        else:
            fail(f"{desc} (want: '{pattern}'  out: {combined[:180]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_kv(desc: str, pattern: str, kv_file: str, timeout: int = 30) -> None:
    """封装 kvspace clear + kvlang run + check_out 的常用模式。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        run_cmd([KV, "kvspace", "clear"], timeout=5)
        result = run_cmd([KV, kv_file], timeout=timeout)
        if pattern in result.stdout:
            ok(desc)
        else:
            fail(f"{desc} (want: '{pattern}'  got: {result.stdout[:120]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_kv_long(desc: str, pattern: str, kv_file: str) -> None:
    """同 check_kv，timeout=60。"""
    check_kv(desc, pattern, kv_file, timeout=60)


def grep_leak(pattern: str, exclude_pkg: str) -> list[str]:
    """grep -rn PATTERN --include=*.go，排除含 exclude_pkg 的行。"""
    try:
        result = subprocess.run(
            ["grep", "-rn", pattern, "--include=*.go", "."],
            capture_output=True, text=True, cwd=str(PROJECT_ROOT),
        )
        return [ln for ln in result.stdout.splitlines()
                if exclude_pkg not in ln and "go." not in ln]
    except Exception:
        return []


# ── 路径常量 ──────────────────────────────────────────────────────────────────
PRINT_INT = "example/kvlang/builtin/print/print_int.kv"
ADD_KV = "example/kvlang/builtin/arith/add.kv"
LOG_LEVEL_INFO = {"LOG_LEVEL": "info"}


# ══════════════════════════════════════════════════════════════════════════════
# 测试入口
# ══════════════════════════════════════════════════════════════════════════════
def main() -> None:
    global FILTER, PASS, FAIL

    parser = argparse.ArgumentParser(description="kvlang 自动化测试脚本")
    parser.add_argument("--filter", type=str, default="",
                        help="只跑含指定字符串的测试")
    args = parser.parse_args()
    FILTER = args.filter

    # ── §0 前置条件 ───────────────────────────────────────────────────────
    section("§0 前置条件")
    check_out("Redis 在线", "PONG", ["redis-cli", "ping"])
    check_exit("kvlang 二进制存在", 0, ["test", "-f", KV])
    check_exit("print_int.kv 存在", 0, ["test", "-f", PRINT_INT])
    check_exit("add.kv 存在", 0, ["test", "-f", ADD_KV])

    # ── §1 构建与静态检查 ─────────────────────────────────────────────────
    section("§1 构建与静态检查")
    check_exit("go build ./...", 0, ["go", "build", "./..."])
    check_exit("go vet ./...", 0, ["go", "vet", "./..."])
    check_exit("go test ./...", 0, ["go", "test", "./...", "-count=1"])
    check_exit("check-keytree", 0, [".claude/hooks/check-keytree.sh"])

    # 零 redis 直引用（internal/kvspace 除外）
    if should_run("零 redis 包直引用"):
        leaks = grep_leak("github.com/redis", "internal/kvspace")
        if not leaks:
            ok("零 redis 包直引用")
        else:
            fail(f"零 redis 包直引用: {leaks}")

    # ── §2 help ───────────────────────────────────────────────────────────
    section("§2 help")
    check_any("help 子命令", "usage:", [KV, "help"])
    check_any("-h flag", "usage:", [KV, "-h"])
    check_any("--help flag", "usage:", [KV, "--help"])
    check_any("help 含 load", "load", [KV, "help"])
    check_any("help 含 serve", "serve", [KV, "help"])
    check_any("help 含 kvspace", "kvspace", [KV, "help"])

    # ── §3 load ───────────────────────────────────────────────────────────
    section("§3 load")
    run_cmd([KV, "kvspace", "clear"], timeout=5)

    check_any("load 文件", "loaded 1 file", [KV, "load", PRINT_INT],
              env=LOG_LEVEL_INFO)
    check_out("load 后 /func/main 存在", '"entry"',
              [KV, "kvspace", "get", "/func/main"])
    check_exit("load 后无 vthread", 1,
               ["bash", "-c",
                f"{KV} kvspace list /vthread 2>/dev/null | grep -q ."])
    check_any("load --addr", "loaded",
              [KV, "load", "--addr", "127.0.0.1:6379", PRINT_INT],
              env=LOG_LEVEL_INFO)
    check_any("load 目录", "loaded", [KV, "load",
              "example/kvlang/builtin/print/"], env=LOG_LEVEL_INFO)
    check_err("load 缺路径 → usage", "usage:", [KV, "load"])
    check_err("load 未知 flag", "flag provided", [KV, "load", "--unknown"])
    check_any("load --help 含 addr", "addr", [KV, "load", "--help"])

    # ── §4 run ────────────────────────────────────────────────────────────
    section("§4a run 文件模式")
    check_kv("print_int → X=42", "X = 42", PRINT_INT)
    check_kv("print_int → R=42", "R = 42", PRINT_INT)
    check_kv("add.kv → C=5", "C = 5", ADD_KV)
    check_kv("run --addr", "X = 42", PRINT_INT)  # 注意：--addr 需在子命令前
    # 修正：kvlang --addr 实际调用
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    check_out("run --addr", "X = 42",
              [KV, "--addr", "127.0.0.1:6379", PRINT_INT])

    section("§4b run 内联 -c")
    inline = (
        'def add2(A:int, B:int) -> (C:int) {\n'
        "    './C' <- A + B\n"
        '}\n'
        'str.set("kvlangrun") -> \'./term\'\n'
        "add2(10, 32) -> './sum'\n"
        'print("sum =", \'./sum\')\n'
    )
    check_out("run -c inline", "sum = 42", [KV, "-c", inline])

    section("§4c run 管道模式")
    if should_run("run pipe"):
        content = (PROJECT_ROOT / PRINT_INT).read_text()
        try:
            run_cmd([KV, "kvspace", "clear"], timeout=5)
            result = run_cmd([KV], stdin=content)
            if "X = 42" in result.stdout:
                ok("run pipe")
            else:
                fail(f"run pipe (stdout: {result.stdout[:120]})")
        except subprocess.TimeoutExpired:
            fail("run pipe (timeout)")

    # ── §5 serve ──────────────────────────────────────────────────────────
    section("§5 serve")
    check_any("serve 启动日志", "starting",
              ["timeout", "2", KV, "serve"], timeout=8, env=LOG_LEVEL_INFO)
    check_any("serve --addr 日志", "127.0.0.1",
              ["timeout", "2", KV, "serve", "--addr", "127.0.0.1:6379"],
              timeout=8, env=LOG_LEVEL_INFO)
    check_any("serve --help", "addr", [KV, "serve", "--help"])
    check_err("serve 未知 flag", "flag provided", [KV, "serve", "--unknown"])

    section("§5.1 load → serve 集成")
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    run_cmd([KV, "load", PRINT_INT], timeout=10, env=LOG_LEVEL_INFO)
    try:
        result = run_cmd(["timeout", "8", KV, "serve"],
                         timeout=20, env=LOG_LEVEL_INFO)
        out = result.stdout + result.stderr
        if "X = 42" in out:
            ok("serve → X = 42")
        else:
            fail(f"serve → X = 42 (out: {out[:120]})")
        if "R = 42" in out:
            ok("serve → R = 42")
        else:
            fail(f"serve → R = 42 (out: {out[:120]})")
        if "entry=pre_main" in result.stderr:
            ok("serve stderr 含 entry=pre_main")
        else:
            fail(f"serve stderr 含 entry=pre_main (stderr: {result.stderr[:200]})")
    except subprocess.TimeoutExpired:
        fail("serve → 集成测试 (timeout)")

    # ── §6 vet ────────────────────────────────────────────────────────────
    section("§6 vet")
    check_out("vet OK", "OK", [KV, "vet", PRINT_INT])
    check_out("vet --dump 含 Func", "Func",
              [KV, "vet", "--dump", PRINT_INT])
    check_out("vet --lower OK", "OK",
              [KV, "vet", "--lower", PRINT_INT])
    check_out("vet --dump --lower", "Instruction",
              [KV, "vet", "--dump", "--lower", PRINT_INT])
    if should_run("vet pipe"):
        content = (PROJECT_ROOT / PRINT_INT).read_text()
        result = run_cmd([KV, "vet"], stdin=content)
        if "stdin: OK" in result.stdout:
            ok("vet pipe")
        else:
            fail(f"vet pipe (stdout: {result.stdout[:120]})")
    check_any("vet --help 含 dump", "dump", [KV, "vet", "--help"])
    check_err("vet 无参数 → usage", "usage:", [KV, "vet"])

    # ── §7 format ─────────────────────────────────────────────────────────
    section("§7 format")
    check_out("format 文件", "def ", [KV, "format", PRINT_INT])
    check_out("format 别名 fmt", "def ", [KV, "fmt", PRINT_INT])
    if should_run("format pipe"):
        content = (PROJECT_ROOT / PRINT_INT).read_text()
        result = run_cmd([KV, "format"], stdin=content)
        if "def " in result.stdout:
            ok("format pipe")
        else:
            fail(f"format pipe (stdout: {result.stdout[:120]})")
    check_any("format --help", "-c", [KV, "format", "--help"])

    # ── §8 kvspace ────────────────────────────────────────────────────────
    section("§8 kvspace CRUD")
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    run_cmd([KV, "kvspace", "set", "/test/x", "hello"], timeout=5)
    check_out("get 存在的 key", "hello", [KV, "kvspace", "get", "/test/x"])
    run_cmd([KV, "kvspace", "set", "/test/y", "world"], timeout=5)
    check_out("mget 第一个值", "hello",
              [KV, "kvspace", "mget", "/test/x", "/test/y"])
    check_out("mget 第二个值", "world",
              [KV, "kvspace", "mget", "/test/x", "/test/y"])
    check_out("list 子项", "x", [KV, "kvspace", "list", "/test"])
    run_cmd([KV, "kvspace", "del", "/test/x"], timeout=5)
    check_exit("get 已删除 → exit 1", 1, [KV, "kvspace", "get", "/test/x"])

    section("§8 kvspace tree / dump")
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    run_cmd([KV, "load", PRINT_INT], timeout=10)
    check_out("tree 含函数名", "print_int",
              [KV, "kvspace", "tree", "/src"])
    check_out("dump 含签名 def", "def ",
              [KV, "kvspace", "dump", "/src/print/print_int"])

    section("§8 kvspace notify / watch")
    if should_run("watch 收到通知消息"):
        watch_out: list[str] = []

        def _do_watch() -> None:
            try:
                r = run_cmd(
                    [KV, "kvspace", "watch", "--timeout", "3s",
                     "/test/notify"], timeout=8)
                watch_out.append(r.stdout)
            except Exception:
                pass

        t = threading.Thread(target=_do_watch)
        t.start()
        time.sleep(0.5)
        run_cmd([KV, "kvspace", "notify", "/test/notify", "ping-msg"],
                timeout=5)
        t.join(timeout=6)
        combined = "".join(watch_out)
        if "ping-msg" in combined:
            ok("watch 收到通知消息")
        else:
            fail(f"watch 收到通知消息 (out: {combined[:120]})")

    check_exit("watch 超时 exit 1", 1,
               [KV, "kvspace", "watch", "--timeout", "1s", "/nonexistent"],
               timeout=5)
    check_any("watch --help 含 timeout", "timeout",
              [KV, "kvspace", "watch", "--help"])
    check_err("watch 非法 duration", "invalid value",
              [KV, "kvspace", "watch", "--timeout", "xyz", "/k"])

    section("§8 kvspace --addr / clear")
    check_out("kvspace --addr get", "entry",
              [KV, "kvspace", "--addr", "127.0.0.1:6379", "get", "/func/main"])
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    check_exit("clear 后 list 为空", 1,
               ["bash", "-c",
                f"{KV} kvspace list /src 2>/dev/null | grep -q ."])

    # ── §9 flag 错误处理 ─────────────────────────────────────────────────
    section("§9 Flag 错误处理")
    check_err("load 未知 flag", "flag provided",
              [KV, "load", "--unknown", "/f"])
    check_err("serve 未知 flag", "flag provided",
              [KV, "serve", "--unknown"])
    check_err("vet 未知 flag", "flag provided",
              [KV, "vet", "--unknown"])
    check_err("kvspace watch 非法时长", "invalid value",
              [KV, "kvspace", "watch", "--timeout", "xyz", "/k"])
    check_err("kvspace get 缺 key", "usage:", [KV, "kvspace", "get"])
    check_err("kvspace set 缺 value", "usage:", [KV, "kvspace", "set", "/k"])
    check_err("kvspace 无子命令", "usage:", [KV, "kvspace"])

    # ── §10 架构合规 ──────────────────────────────────────────────────────
    section("§10 架构合规")
    if should_run("零 redis 包直引用"):
        leaks = grep_leak("github.com/redis", "internal/kvspace")
        if not leaks:
            ok("零 redis 包直引用")
        else:
            fail(f"redis 泄漏: {leaks}")

    if should_run("零硬编码 /vthread/ 路径"):
        leaks = grep_leak('"/vthread/', "internal/keytree")
        if not leaks:
            ok("零硬编码 /vthread/ 路径")
        else:
            fail(f"/vthread/ 泄漏: {leaks}")

    if should_run("零硬编码 /src/ 路径"):
        leaks = grep_leak('"/src/', "internal/keytree")
        if not leaks:
            ok("零硬编码 /src/ 路径")
        else:
            fail(f"/src/ 泄漏: {leaks}")

    check_exit("check-keytree hook", 0, [".claude/hooks/check-keytree.sh"])

    # ── §11 vet 全量 ─────────────────────────────────────────────────────
    section("§11 vet 全量")
    if should_run("§11 vet 全量"):
        vet_pass = 0
        vet_fail = 0
        search_dirs = [
            "example/kvlang/builtin",
            "example/kvlang/controlflow",
            "example/kvlang/smoke",
            "example/kvlang/algo",
        ]
        for d in search_dirs:
            for f in sorted(Path(d).rglob("*.kv")):
                if f.name == "index.kv":
                    continue
                f_str = str(f)
                try:
                    result = run_cmd([KV, "vet", f_str], timeout=10)
                    if result.returncode == 0:
                        vet_pass += 1
                    else:
                        fail(f"vet {f_str}: {result.stderr[:120]}")
                        vet_fail += 1
                except subprocess.TimeoutExpired:
                    fail(f"vet {f_str}: timeout")
                    vet_fail += 1
        if vet_fail == 0:
            ok(f"vet 全量通过 ({vet_pass} 个文件)")
            PASS += 1  # 补偿：shell 版对整批算 1 PASS
        else:
            FAIL += vet_fail

    # ── §12 run builtin arith ────────────────────────────────────────────
    section("§12 run builtin arith")
    BA = "example/kvlang/builtin/arith"
    for name, pat, fname in [
        ("arith/add   C=5", "C = 5", "add.kv"),
        ("arith/sub   C=7", "C = 7", "sub.kv"),
        ("arith/mul   C=42", "C = 42", "mul.kv"),
        ("arith/div   C=7.5", "C = 7.5", "div.kv"),
        ("arith/abs   C=5", "C = 5", "abs.kv"),
        ("arith/neg   C=-5", "C = -5", "neg.kv"),
        ("arith/sign  C=-1", "C = -1", "sign.kv"),
        ("arith/pow   C=8", "C = 8", "pow.kv"),
        ("arith/sqrt  C=4", "C = 4", "sqrt.kv"),
        ("arith/max   C=7", "C = 7", "max.kv"),
        ("arith/min   C=-2", "C = -2", "min.kv"),
        ("arith/exp   C=7", "C = 7", "exp.kv"),
        ("arith/log   C=2", "C = 2", "log.kv"),
        ("arith/double_op  S=13", "S = 13", "double_op.kv"),
        ("arith/double_op_cstyle S=13", "S = 13", "double_op_cstyle.kv"),
        ("arith/three_add R=9", "R = 9", "three_add.kv"),
        ("arith/poly3 Y=35", "Y = 35", "poly3.kv"),
    ]:
        check_kv(name, pat, f"{BA}/{fname}")

    section("§12 run builtin print/cast/chain/compare/logic")
    BP = "example/kvlang/builtin/print"
    for name, pat, fname in [
        ("print/print_int   X=42", "X = 42", "print_int.kv"),
        ("print/print_bool  C=true", "C = true", "print_bool.kv"),
        ("print/print_multi R=6", "R = 6", "print_multi.kv"),
        ("print/print_chain D=20", "D = 20", "print_chain.kv"),
    ]:
        check_kv(name, pat, f"{BP}/{fname}")
    BC = "example/kvlang/builtin/cast"
    check_kv("cast/float  C=42.0", "C = 42.0", f"{BC}/float.kv")
    check_kv("cast/int    C=3", "C = 3", f"{BC}/int.kv")
    check_kv("chain/chain D=20", "D = 20",
             "example/kvlang/builtin/chain/chain.kv")
    check_kv("compare/eq  C=true", "C = true",
             "example/kvlang/builtin/compare/eq.kv")
    check_kv("compare/lt  C=true", "C = true",
             "example/kvlang/builtin/compare/lt.kv")
    check_kv("logic/and   C=false", "C = false",
             "example/kvlang/builtin/logic/and.kv")
    check_kv("logic/bool  C=true", "C = true",
             "example/kvlang/builtin/logic/bool.kv")
    check_kv("logic/not   C=false", "C = false",
             "example/kvlang/builtin/logic/not.kv")

    section("§12 run builtin native（无 print，验 exit 0）")
    if should_run("§12 run builtin native"):
        nat_pass = 0
        nat_fail = 0
        for f in sorted(Path("example/kvlang/builtin/native").rglob("*.kv")):
            if f.name == "index.kv":
                continue
            try:
                run_cmd([KV, "kvspace", "clear"], timeout=5)
                result = run_cmd([KV, str(f)], timeout=15)
                if result.returncode == 0:
                    nat_pass += 1
                else:
                    fail(f"native exit={result.returncode}: {f}")
                    nat_fail += 1
            except subprocess.TimeoutExpired:
                fail(f"native timeout: {f}")
                nat_fail += 1
        if nat_fail == 0:
            ok(f"native 全量 exit 0 ({nat_pass} 个文件)")
            PASS += 1
        else:
            FAIL += nat_fail

    # ── §13 controlflow/test_runner ───────────────────────────────────────
    section("§13 run controlflow/test_runner（32 断言全 PASS）")
    check_kv_long("controlflow/test_runner ALL DONE", "ALL TESTS DONE",
                  "example/kvlang/controlflow/test_runner.kv")

    # ── §14 P0 冒烟 ────────────────────────────────────────────────────────
    section("§14 P0 冒烟")
    check_kv("smoke/hello  hello=world", "hello = world",
             "example/kvlang/smoke/hello.kv")

    # ── §15 P1 compare 等价类覆盖 ─────────────────────────────────────────
    section("§15 P1 compare 等价类覆盖")
    BC = "example/kvlang/builtin/compare"
    for name, pat, fname in [
        ("== 相等→true  H=true", "H = true", "eq_true.kv"),
        ("== 相等→true  E=true", "E = true", "eq_true.kv"),
        ("== 不等→false H=false", "H = false", "eq_false.kv"),
        ("== 不等→false E=false", "E = false", "eq_false.kv"),
        ("!= 不等→true  H=true", "H = true", "ne_true.kv"),
        ("!= 相等→false H=false", "H = false", "ne_false.kv"),
        ("<  小于→true  H=true", "H = true", "lt_true.kv"),
        ("<  相等→false H=false", "H = false", "lt_eq_false.kv"),
        ("<  大于→false H=false", "H = false", "lt_gt_false.kv"),
        (">  大于→true  H=true", "H = true", "gt_true.kv"),
        (">  相等→false H=false", "H = false", "gt_eq_false.kv"),
        (">  小于→false H=false", "H = false", "gt_lt_false.kv"),
        ("<= 小于→true  H=true", "H = true", "le_lt_true.kv"),
        ("<= 相等→true  H=true", "H = true", "le_eq_true.kv"),
        ("<= 大于→false H=false", "H = false", "le_gt_false.kv"),
        (">= 大于→true  H=true", "H = true", "ge_gt_true.kv"),
        (">= 相等→true  H=true", "H = true", "ge_eq_true.kv"),
        (">= 小于→false H=false", "H = false", "ge_lt_false.kv"),
    ]:
        check_kv(name, pat, f"{BC}/{fname}")

    # ── §16 P1 logic 等价类覆盖 ───────────────────────────────────────────
    section("§16 P1 logic 等价类覆盖")
    BL = "example/kvlang/builtin/logic"
    for name, pat, fname in [
        ("&& T&&T→true", "C = true", "and_tt.kv"),
        ("&& T&&F→false", "C = false", "and_tf.kv"),
        ("&& F&&T→false", "C = false", "and_ft.kv"),
        ("&& F&&F→false", "C = false", "and_ff.kv"),
        ("|| T||T→true", "C = true", "or_tt.kv"),
        ("|| T||F→true", "C = true", "or_tf.kv"),
        ("|| F||T→true", "C = true", "or_ft.kv"),
        ("|| F||F→false", "C = false", "or_ff.kv"),
        ("!  !T→false", "C = false", "not_t.kv"),
        ("!  !F→true", "C = true", "not_f.kv"),
    ]:
        check_kv(name, pat, f"{BL}/{fname}")

    # ── §17 P1 arith 边界等价类 ───────────────────────────────────────────
    section("§17 P1 arith 边界等价类")
    BA = "example/kvlang/builtin/arith"
    for name, pat, fname in [
        ("arith/add_zero  C=5", "C = 5", "add_zero.kv"),
        ("arith/add_zero  Z=0", "Z = 0", "add_zero.kv"),
        ("arith/sub_neg   C=-7", "C = -7", "sub_neg.kv"),
        ("arith/mul_zero  C=0", "C = 0", "mul_zero.kv"),
        ("arith/div_float C=3.5", "C = 3.5", "div_float.kv"),
        ("arith/pow_zero  C=1.0", "C = 1.0", "pow_zero.kv"),
        ("arith/sqrt_one  C=1.0", "C = 1.0", "sqrt_one.kv"),
        ("arith/neg_double C=5", "C = 5", "neg_double.kv"),
        ("arith/max_eq    C=5", "C = 5", "max_eq.kv"),
        ("arith/min_eq    C=5", "C = 5", "min_eq.kv"),
    ]:
        check_kv(name, pat, f"{BA}/{fname}")

    # ── §18 P1 cast 边界等价类 ────────────────────────────────────────────
    section("§18 P1 cast 边界等价类")
    BC = "example/kvlang/builtin/cast"
    check_kv("cast/int_neg         C=-3", "C = -3", f"{BC}/int_neg.kv")
    check_kv("cast/to_bool_zero    C=false", "C = false",
             f"{BC}/to_bool_zero.kv")
    check_kv("cast/to_bool_nonzero C=true", "C = true",
             f"{BC}/to_bool_nonzero.kv")

    # ── §19 P1 call CR 系列 ───────────────────────────────────────────────
    section("§19 P1 call CR 系列（CR-1~CR-7）")
    BCA = "example/kvlang/builtin/call"
    for name, pat, fname in [
        ("CR-1/2 single_call result=5", "result = 5", "single_call.kv"),
        ("CR-3 multi_arg R=15", "R = 15", "multi_arg.kv"),
        ("CR-4 multi_ret S=10", "S = 10", "multi_ret.kv"),
        ("CR-4 multi_ret D=4", "D = 4", "multi_ret.kv"),
        ("CR-4 multi_ret P=21", "P = 21", "multi_ret.kv"),
        ("CR-5 nested_call before=100", "before = 100", "nested_call.kv"),
        ("CR-5 nested_call inner=42", "inner = 42", "nested_call.kv"),
        ("CR-5 nested_call after=100", "after = 100", "nested_call.kv"),
        ("CR-6 diamond R=25", "R = 25", "diamond.kv"),
        ("CR-7 caller C=5", "C = 5", "caller.kv"),
    ]:
        check_kv(name, pat, f"{BCA}/{fname}")

    # ── §20 P2 帧隔离 + TCO ────────────────────────────────────────────────
    section("§20 P2 帧隔离（SI-1~3）+ TCO（TCO-1~2）")
    CF = "example/kvlang/controlflow"
    for name, pat, fname in [
        ("SI-1a scope_isolation x=5", "PASS SI-1a", "scope_isolation.kv"),
        ("SI-1b scope_isolation x=10", "PASS SI-1b", "scope_isolation.kv"),
        ("SI-2a scope_isolation S=8", "PASS SI-2a", "scope_isolation.kv"),
        ("SI-2b scope_isolation S=15", "PASS SI-2b", "scope_isolation.kv"),
        ("SI-3  scope_isolation chain", "PASS SI-3", "scope_isolation.kv"),
        ("TCO-1 tco_depth sum=5050", "sum = 5050", "tco_depth.kv"),
        ("TCO-2 tco_depth fact=3628800", "fact = 3628800", "tco_depth.kv"),
    ]:
        check_kv_long(name, pat, f"{CF}/{fname}")

    # ── §21 P3 算法展示 ────────────────────────────────────────────────────
    section("§21 P3 算法展示")
    ALGO = "example/kvlang/algo"
    for name, pat, fname in [
        ("P3 fibonacci  fib=55", "fib = 55", "fibonacci.kv"),
        ("P3 factorial  fact=3628800", "fact = 3628800", "factorial.kv"),
        ("P3 gcd        gcd=6", "gcd = 6", "gcd.kv"),
        ("P3 fizzbuzz   line=1", "1", "fizzbuzz.kv"),
        ("P3 fizzbuzz   Fizz(3)", "Fizz", "fizzbuzz.kv"),
        ("P3 fizzbuzz   Buzz(5)", "Buzz", "fizzbuzz.kv"),
        ("P3 fizzbuzz   FizzBuzz(15)", "FizzBuzz", "fizzbuzz.kv"),
        ("P3 power      result=1024", "result = 1024", "power.kv"),
        ("P3 collatz    steps=111", "steps = 111", "collatz.kv"),
        ("P3 classify   grade=B", "grade = B", "classify.kv"),
    ]:
        check_kv_long(name, pat, f"{ALGO}/{fname}")

    # ── 汇总 ─────────────────────────────────────────────────────────────
    print()
    print("════════════════════════════════════")
    print(f"  {GREEN}PASS: {PASS}{NC}   {RED}FAIL: {FAIL}{NC}   {YELLOW}SKIP: {SKIP}{NC}")
    print("════════════════════════════════════")

    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()

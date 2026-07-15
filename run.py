#!/usr/bin/env python3
"""kvlang 自动化测试脚本

用法（在项目根目录执行）:
    python3 run.py                    # 全量测试
    python3 run.py --filter serve     # 只跑含 "serve" 的测试
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
PROJECT_ROOT = Path(__file__).resolve().parent
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
        run_cmd([KV, "kvspace", "clear"], timeout=5)
        result = run_cmd(cmd, timeout=timeout, env=env)
        if pattern in result.stdout:
            ok(desc)
        else:
            fail(f"{desc} (want: '{pattern}'  got stdout: {result.stdout[:120]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_err(desc: str, pattern: str, cmd: list[str], timeout: int = 30,
              env: dict[str, str] | None = None) -> None:
    """CMD stderr 含 PATTERN。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        if pattern in result.stderr:
            ok(desc)
        else:
            fail(f"{desc} (want in stderr: '{pattern}'  got: {result.stderr[:120]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_exit(desc: str, want_exit: int, cmd: list[str], timeout: int = 30,
               env: dict[str, str] | None = None) -> None:
    """CMD 退出码为 WANT_EXIT。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        if result.returncode == want_exit:
            ok(desc)
        else:
            fail(f"{desc} (want exit {want_exit}, got {result.returncode})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_any(desc: str, pattern: str, cmd: list[str], timeout: int = 30,
              env: dict[str, str] | None = None) -> None:
    """CMD stdout 或 stderr 含 PATTERN。"""
    if not should_run(desc):
        skip(desc)
        return
    try:
        result = run_cmd(cmd, timeout=timeout, env=env)
        combined = result.stdout + result.stderr
        if pattern in combined:
            ok(desc)
        else:
            fail(f"{desc} (want: '{pattern}'  got: {combined[:120]})")
    except subprocess.TimeoutExpired:
        fail(f"{desc} (timeout)")


def check_kv(desc: str, pattern: str, kv_file: str, timeout: int = 30) -> None:
    """运行 kv_file，stdout 含 PATTERN。"""
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
# tutorial/04-func は load/serve/vet/format テストの基準ファイル
FUNC_KV = "tutorial/04-func/main.kv"
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
    check_exit("tutorial/04-func/main.kv 存在", 0, ["test", "-f", FUNC_KV])

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

    check_any("load 文件", "loaded 1 file", [KV, "load", FUNC_KV],
              env=LOG_LEVEL_INFO)
    check_out("load 后 /func/main 存在", '"entry"',
              [KV, "kvspace", "get", "/func/main"])
    check_exit("load 后无 vthread", 1,
               ["bash", "-c",
                f"{KV} kvspace list /vthread 2>/dev/null | grep -q ."])
    check_any("load --addr", "loaded",
              [KV, "load", "--addr", "127.0.0.1:6379", FUNC_KV],
              env=LOG_LEVEL_INFO)
    check_any("load 目录", "loaded", [KV, "load",
              "tutorial/04-func/"], env=LOG_LEVEL_INFO)
    check_err("load 缺路径 → usage", "usage:", [KV, "load"])
    check_err("load 未知 flag", "flag provided", [KV, "load", "--unknown"])
    check_any("load --help 含 addr", "addr", [KV, "load", "--help"])

    # ── §4 run ────────────────────────────────────────────────────────────
    section("§4a run 文件模式")
    check_kv("04-func add(10,20)=30", "add(10,20) = 30", FUNC_KV)
    check_kv("04-func double=60",     "double(add(10,20)) = 60", FUNC_KV)
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    check_out("run --addr", "add(10,20) = 30",
              [KV, "--addr", "127.0.0.1:6379", FUNC_KV])

    section("§4b run 内联 -c")
    inline = (
        'def add2(A:int, B:int) -> (C:int) {\n'
        "    './C' <- A + B\n"
        '}\n'
        '"kvlangrun" -> \'./term\'\n'
        "add2(10, 32) -> './sum'\n"
        'print("sum =", \'./sum\')\n'
    )
    check_out("run -c inline", "sum = 42", [KV, "-c", inline])

    section("§4c run 管道模式")
    if should_run("run pipe"):
        content = (PROJECT_ROOT / FUNC_KV).read_text()
        try:
            run_cmd([KV, "kvspace", "clear"], timeout=5)
            result = run_cmd([KV], stdin=content)
            if "add(10,20) = 30" in result.stdout:
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
    run_cmd([KV, "load", FUNC_KV], timeout=10, env=LOG_LEVEL_INFO)
    try:
        result = run_cmd(["timeout", "8", KV, "serve"],
                         timeout=20, env=LOG_LEVEL_INFO)
        out = result.stdout + result.stderr
        if "add(10,20) = 30" in out:
            ok("serve → add(10,20) = 30")
        else:
            fail(f"serve → add(10,20) = 30 (out: {out[:120]})")
        if "entry=pre_main" in result.stderr:
            ok("serve stderr 含 entry=pre_main")
        else:
            fail(f"serve stderr 含 entry=pre_main (stderr: {result.stderr[:200]})")
    except subprocess.TimeoutExpired:
        fail("serve → 集成测试 (timeout)")

    # ── §6 vet ────────────────────────────────────────────────────────────
    section("§6 vet")
    check_out("vet OK", "OK", [KV, "vet", FUNC_KV])
    check_out("vet --dump 含 Func", "Func",
              [KV, "vet", "--dump", FUNC_KV])
    check_out("vet --lower OK", "OK",
              [KV, "vet", "--lower", FUNC_KV])
    check_out("vet --dump --lower", "Instruction",
              [KV, "vet", "--dump", "--lower", FUNC_KV])
    if should_run("vet pipe"):
        content = (PROJECT_ROOT / FUNC_KV).read_text()
        result = run_cmd([KV, "vet"], stdin=content)
        if "stdin: OK" in result.stdout:
            ok("vet pipe")
        else:
            fail(f"vet pipe (stdout: {result.stdout[:120]})")
    check_any("vet --help 含 dump", "dump", [KV, "vet", "--help"])
    check_err("vet 无参数 → usage", "usage:", [KV, "vet"])

    # ── §7 format ─────────────────────────────────────────────────────────
    section("§7 format")
    check_out("format 文件", "def ", [KV, "format", FUNC_KV])
    check_out("format 别名 fmt", "def ", [KV, "fmt", FUNC_KV])
    if should_run("format pipe"):
        content = (PROJECT_ROOT / FUNC_KV).read_text()
        result = run_cmd([KV, "format"], stdin=content)
        if "def " in result.stdout:
            ok("format pipe")
        else:
            fail(f"format pipe (stdout: {result.stdout[:120]})")
    check_any("format --help", "-c", [KV, "format", "--help"])

    # ── §8 kvspace ────────────────────────────────────────────────────────
    section("§8 kvspace CRUD")
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    check_out("set hello", "ok", [KV, "kvspace", "set", "/t/hello", "world"])
    check_out("get hello", "world", [KV, "kvspace", "get", "/t/hello"])
    check_out("del hello", "ok", [KV, "kvspace", "del", "/t/hello"])
    check_exit("get deleted → exit 1", 1,
               ["bash", "-c", f"{KV} kvspace get /t/hello 2>/dev/null | grep -q ."])

    section("§8 kvspace tree / dump")
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    run_cmd([KV, "kvspace", "set", "/t/a", "1"], timeout=5)
    run_cmd([KV, "kvspace", "set", "/t/b", "2"], timeout=5)
    check_out("list /t", "/t/", [KV, "kvspace", "list", "/t"])
    check_out("tree /t 含 /t/a", "/t/a", [KV, "kvspace", "tree", "/t"])
    check_out("dump /t 含 value", "1", [KV, "kvspace", "dump", "/t"])

    section("§8 kvspace notify / watch")
    if should_run("notify/watch"):
        run_cmd([KV, "kvspace", "clear"], timeout=5)
        notify_done = threading.Event()
        notify_result: list[str] = []

        def do_watch() -> None:
            try:
                r = run_cmd([KV, "kvspace", "watch", "/t/evt"], timeout=10)
                notify_result.append(r.stdout)
            except subprocess.TimeoutExpired:
                notify_result.append("timeout")
            finally:
                notify_done.set()

        t = threading.Thread(target=do_watch, daemon=True)
        t.start()
        time.sleep(0.3)
        run_cmd([KV, "kvspace", "notify", "/t/evt", "ping"], timeout=5)
        notify_done.wait(timeout=12)
        if notify_result and "ping" in notify_result[0]:
            ok("notify/watch ping")
        else:
            fail(f"notify/watch (got: {notify_result})")

    section("§8 kvspace --addr / clear")
    check_any("kvspace --addr set", "ok",
              [KV, "kvspace", "--addr", "127.0.0.1:6379", "set", "/t/addr", "yes"])
    check_any("kvspace --addr get", "yes",
              [KV, "kvspace", "--addr", "127.0.0.1:6379", "get", "/t/addr"])
    run_cmd([KV, "kvspace", "clear"], timeout=5)
    check_exit("after clear get 空", 1,
               ["bash", "-c", f"{KV} kvspace get /t/addr 2>/dev/null | grep -q ."])

    # ── §9 Flag 错误处理 ──────────────────────────────────────────────────
    section("§9 Flag 错误处理")
    check_err("run 未知 flag",     "flag provided", [KV, "--unknown-flag", FUNC_KV])
    check_err("vet 未知 flag",     "flag provided", [KV, "vet", "--unknown"])
    check_err("format 未知 flag",  "flag provided", [KV, "format", "--unknown"])
    check_err("kvspace 未知 flag", "flag provided", [KV, "kvspace", "--unknown"])
    check_err("load 未知 flag",    "flag provided", [KV, "load", "--unknown"])

    # ── §10 架构合规 ──────────────────────────────────────────────────────
    section("§10 架构合规")
    if should_run("vthread 泄漏"):
        leaks = grep_leak("/vthread/", "vthread")
        if not leaks:
            ok("零 /vthread/ 字符串直引用")
        else:
            fail(f"/vthread/ 泄漏: {leaks[:3]}")

    # ── §11 vet 全量 ─────────────────────────────────────────────────────
    section("§11 vet 全量（tutorial/）")
    if should_run("§11 vet 全量"):
        vet_pass = 0
        vet_fail = 0
        for f in sorted(Path("tutorial").rglob("*.kv")):
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
            PASS += 1

    # ── §12 tutorial 分步测试 ─────────────────────────────────────────────
    section("§12 tutorial 分步测试")
    TUT = "tutorial"
    for desc, pattern, kv_file in [
        ("01-hello print",         "hello kvlang",          f"{TUT}/01-hello/main.kv"),
        ("02-vars x=42",           "x = 42",                f"{TUT}/02-vars/main.kv"),
        ("02-vars y=50",           "y = 50",                f"{TUT}/02-vars/main.kv"),
        ("03-arith add:13",        "add: 13",               f"{TUT}/03-arith/main.kv"),
        ("03-arith pow:32",        "pow: 32",               f"{TUT}/03-arith/main.kv"),
        ("03-arith sqrt:12",       "sqrt: 12",              f"{TUT}/03-arith/main.kv"),
        ("04-func add(10,20)=30",  "add(10,20) = 30",       f"{TUT}/04-func/main.kv"),
        ("04-func double=60",      "double(add(10,20)) = 60", f"{TUT}/04-func/main.kv"),
        ("05-if abs(-5)=5",        "abs(-5) = 5",           f"{TUT}/05-if/main.kv"),
        ("05-if &&=false",         "false",                 f"{TUT}/05-if/main.kv"),
        ("06-while sum=55",        "sum(1..10) = 55",       f"{TUT}/06-while/main.kv"),
        ("06-while break div7=7",  "first div7 in [1,20] = 7", f"{TUT}/06-while/main.kv"),
        ("06-while continue odd=25", "sum odds(1..10) = 25", f"{TUT}/06-while/main.kv"),
        ("07-recursion fib=55",    "fib(10) = 55",          f"{TUT}/07-recursion/main.kv"),
        ("07-recursion fact=3628800", "fact(10) = 3628800", f"{TUT}/07-recursion/main.kv"),
    ]:
        check_kv(desc, pattern, kv_file)

    # ── §13 algo 展示 ─────────────────────────────────────────────────────
    section("§13 algo 展示")
    ALGO = "tutorial/08-algo"
    for desc, pattern, fname in [
        ("fibonacci fib=55",      "fib = 55",      "fibonacci.kv"),
        ("factorial fact=3628800","fact = 3628800", "factorial.kv"),
        ("fizzbuzz 1",            "1",              "fizzbuzz.kv"),
        ("fizzbuzz Fizz",         "Fizz",           "fizzbuzz.kv"),
        ("fizzbuzz Buzz",         "Buzz",           "fizzbuzz.kv"),
        ("fizzbuzz FizzBuzz",     "FizzBuzz",       "fizzbuzz.kv"),
        ("gcd gcd=6",             "gcd = 6",        "gcd.kv"),
        ("power result=1024",     "result = 1024",  "power.kv"),
        ("collatz steps=111",     "steps = 111",    "collatz.kv"),
        ("classify grade=B",      "grade = B",      "classify.kv"),
    ]:
        check_kv_long(desc, pattern, f"{ALGO}/{fname}")

    # ── §14 帧隔离 + TCO ──────────────────────────────────────────────────
    section("§14 帧隔离（SI-1~3）+ TCO（TCO-1~2）")
    CF = "tutorial/08-algo"
    for desc, pattern, fname in [
        ("SI-1a scope_isolation x=5",   "PASS SI-1a", "scope_isolation.kv"),
        ("SI-1b scope_isolation x=10",  "PASS SI-1b", "scope_isolation.kv"),
        ("SI-2a scope_isolation S=8",   "PASS SI-2a", "scope_isolation.kv"),
        ("SI-2b scope_isolation S=15",  "PASS SI-2b", "scope_isolation.kv"),
        ("SI-3  scope_isolation chain", "PASS SI-3",  "scope_isolation.kv"),
        ("TCO-1 tco_depth sum=5050",    "sum = 5050",     "tco_depth.kv"),
        ("TCO-2 tco_depth fact=3628800","fact = 3628800", "tco_depth.kv"),
    ]:
        check_kv_long(desc, pattern, f"{CF}/{fname}")

    # ── 汇总 ─────────────────────────────────────────────────────────────
    print()
    print("════════════════════════════════════")
    print(f"  {GREEN}PASS: {PASS}{NC}   {RED}FAIL: {FAIL}{NC}   {YELLOW}SKIP: {SKIP}{NC}")
    print("════════════════════════════════════")

    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()

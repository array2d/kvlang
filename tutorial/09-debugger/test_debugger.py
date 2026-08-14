#!/usr/bin/env python3
"""debugger() 断点演示 — 单文件执行模式，agent 驱动 debugger() 暂停与恢复。

用法:
  # 终端 A：启动 serve + agent
  python3 tutorial/03-debugger/test_debugger.py

  # 或手动分步调试：
  # 1. kvspace clear && ./kvlang layoutrwir tutorial/03-debugger/breakpoint.kv
  # 2. 终端 A: ./kvlang serve --kvspace redis://127.0.0.1:6379
  # 3. 终端 B: kvspace set /vthread/1/.debugger step
  # 4. 终端 B: kvspace set /lib/main '{"entry":"init","reads":[],"writes":[]}'
  # 5. 终端 B: kvspace watch /vthread/1/.debugger.pause  # 等待断点
  # 6. 终端 B: kvspace notify /vthread/1/.debugger.resume step  # 单步
"""

import json, subprocess, sys, time, os, signal
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
KV = str(ROOT / "kvlang")


def kv(*args):
    r = subprocess.run(["kvspace"] + list(args), capture_output=True, text=True, timeout=5)
    out = r.stdout.strip()
    if r.returncode != 0 and r.stderr.strip():
        out = r.stderr.strip()
    return out


def find_vtid():
    """在 /vthread/ 下找活跃的 vtid（排除 seq/ready/run 等系统键）。"""
    for _ in range(10):
        lines = kv("list", "/vthread/")
        for line in lines.split('\n'):
            line = line.strip()
            if not line:
                continue
            # 找形如 /vthread/N 的一级子键
            parts = line.split('/')
            if len(parts) >= 3 and parts[-1] == '':
                name = parts[-2]
                if name.isdigit():
                    return name
        time.sleep(0.5)
    return None


def test_breakpoint():
    """演示 debugger() 内联断点：loop(3) 每次迭代在 debugger() 处暂停。"""
    file = str(ROOT / "tutorial" / "03-debugger" / "breakpoint.kv")

    kv("clear")
    subprocess.run([KV, "layoutrwir", file], capture_output=True, timeout=10)

    # 启动 serve
    env = os.environ.copy()
    env["LOG_LEVEL"] = "error"
    proc = subprocess.Popen(
        [KV, "serve", "--kvspace", "redis://127.0.0.1:6379"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
    )
    time.sleep(1)

    try:
        # 写 entry 前设 debugger（无法预知 vtid，用路径 watch 捕获）
        # 写 entry
        kv("set", "/lib/main", '{"entry":"init","reads":[],"writes":[]}')
        time.sleep(0.5)

        vtid = find_vtid()
        if not vtid:
            print("❌ 未找到活跃 vtid")
            sys.exit(1)

        print(f"vtid={vtid}")
        kv("set", f"/vthread/{vtid}/.debugger", "step")

        # 驱动单步：等待每个 pause 事件
        step = 0
        bp_hits = 0
        i_values = []

        for _ in range(200):
            ev_raw = kv("watch", f"/vthread/{vtid}/.debugger.pause", "--timeout", "3")
            if not ev_raw:
                # 检查是否已结束
                st = kv("get", f"/vthread/{vtid}/.status")
                if "WRONGTYPE" in st or not st or "not found" in st:
                    break
                continue

            try:
                ev = json.loads(ev_raw)
            except json.JSONDecodeError:
                continue

            step += 1
            op = ev.get("op", ev.get("opcode", ""))
            frame = ev.get("frame", "")

            if op == "debugger":
                bp_hits += 1
                i_val = kv("get", f"{frame}/i")
                i_values.append(i_val)
                print(f"  [step {step}] debugger() 断点: i={i_val}")
                # 单步到 print(i)
                kv("notify", f"/vthread/{vtid}/.debugger.resume", "step")
                # 继续单步直到下一个 debugger
                step += 1  # print(i) 也会产生一个 pause
                ev2_raw = kv("watch", f"/vthread/{vtid}/.debugger.pause", "--timeout", "3")
                if ev2_raw:
                    kv("notify", f"/vthread/{vtid}/.debugger.resume", "step")
            else:
                # 非 debugger 指令 → step
                kv("notify", f"/vthread/{vtid}/.debugger.resume", "step")

        # 最后 continue 清除 debug 标志正常退出
        kv("notify", f"/vthread/{vtid}/.debugger.resume", "continue")

        # 验证
        print(f"  断点命中: {bp_hits} 次, i 值: {i_values}")
        assert bp_hits == 3, f"期望 3 次断点, 实际 {bp_hits}"
        assert i_values == ["int:1", "int:2", "int:3"], f"期望 [int:1,int:2,int:3], 实际 {i_values}"
        print("✅ PASS")

    finally:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
        kv("clear")


if __name__ == "__main__":
    test_breakpoint()

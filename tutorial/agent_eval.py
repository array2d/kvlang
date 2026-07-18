#!/usr/bin/env python3
"""agent_eval — 验证外部 LLM 以 README 为教学文档的 kvlang 自适应正确率。

用法:
  export KVLANG_EVAL_API_BASE=https://...   # OpenAI 兼容 API base（不含 /v1）
  export KVLANG_EVAL_API_KEY=sk-...
  export KVLANG_EVAL_MODEL=qwen3.7-plus     # 可选，默认 qwen3.7-plus
  python3 tutorial/agent_eval.py

对每个任务：README + 任务描述 → LLM 生成 kvlang 代码 → 实际运行 → stdout 严格比对。
生成代码与失败详情保存于 /tmp/agent_eval/。
"""
from __future__ import annotations
import json, os, re, subprocess, sys, urllib.request, uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KV = str(ROOT / "kvlang")
OUT = Path("/tmp/agent_eval")

API_BASE = os.environ.get("KVLANG_EVAL_API_BASE", "").rstrip("/")
API_KEY = os.environ.get("KVLANG_EVAL_API_KEY", "")
MODEL = os.environ.get("KVLANG_EVAL_MODEL", "qwen3.7-plus")

# (任务名, 任务描述, 期望 stdout 行)
TASKS = [
    ("hello", "打印一行：hello, kvlang", ["hello, kvlang"]),
    ("arith", "计算 (3+4)*5 并打印结果", ["35"]),
    ("ifelse", "判断 7 是否为奇数，是则打印 odd，否则打印 even", ["odd"]),
    ("loop", "用 while 循环打印 1 到 5，每行一个数字", ["1", "2", "3", "4", "5"]),
    ("func", "定义函数 add 求两数之和（写参形式），调用 add(3,4) 并打印结果", ["7"]),
    ("fib", "计算斐波那契数 fib(10)（fib(1)=1, fib(2)=1）并打印", ["55"]),
    ("dict", '用 dict 字面量创建 d = { name="kv"; ver=1 }，分两行打印 d.name 和 d.ver', ["kv", "1"]),
    ("hash", '统计字符串 "aabbc" 中字符 a 的出现次数并打印（提示：char(s,i) 取第 i 个字符，strlen(s) 取长度）', ["2"]),
    ("list", '构建三节点链表（值 1、2、3，next 存下一节点的绝对路径字符串，尾节点 next 为 ""），遍历并逐行打印节点值', ["1", "2", "3"]),
    ("array", "求数组 [5, 3, 8] 的最大值并打印（可用循环或 max）", ["8"]),
]

SYSTEM = """你是 kvlang 程序员。kvlang 是一门全新语言，下面的 README 是它的完整教学文档，语法以其中示例为准，不要套用其它语言的语法直觉。
只输出可直接运行的 kvlang 代码：顶层直接写语句或 def+调用；不要 markdown 围栏、不要解释文字。"""


def chat(readme: str, task: str) -> str:
    # 每请求独立 session：全新 UUID 作 user 标识与会话头，messages 仅含本任务，
    # 杜绝网关侧会话粘性/上一轮对话影响
    sid = str(uuid.uuid4())
    req = urllib.request.Request(
        API_BASE + "/v1/chat/completions",
        data=json.dumps({
            "model": MODEL,
            "temperature": 0,
            "user": sid,
            "messages": [
                {"role": "system", "content": SYSTEM},
                {"role": "user", "content": f"# kvlang README（教学文档）\n\n{readme}\n\n---\n\n任务：{task}\n程序 stdout 必须恰好满足任务要求，逐行精确。只输出 kvlang 代码。"},
            ],
        }).encode(),
        headers={
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json",
            "X-Session-Id": sid,
            "Connection": "close",
        },
    )
    with urllib.request.urlopen(req, timeout=180) as r:
        body = json.loads(r.read())
    return body["choices"][0]["message"]["content"]


def strip_fences(code: str) -> str:
    code = code.strip()
    m = re.match(r"^```[\w]*\n(.*?)\n?```$", code, re.S)
    return m.group(1) if m else code


def run_kv(path: Path) -> tuple[str, str]:
    subprocess.run(["kvspace", "clear"], capture_output=True, timeout=10)
    r = subprocess.run([KV, str(path)], capture_output=True, text=True, timeout=60, cwd=str(ROOT))
    return r.stdout, r.stderr


def main() -> None:
    if not API_BASE or not API_KEY:
        sys.exit("需设置 KVLANG_EVAL_API_BASE / KVLANG_EVAL_API_KEY 环境变量")
    readme = (ROOT / "README.md").read_text()
    OUT.mkdir(parents=True, exist_ok=True)
    passed = 0
    for name, task, expect in TASKS:
        try:
            code = strip_fences(chat(readme, task))
        except Exception as e:  # noqa: BLE001 — 网络层失败按任务失败计
            print(f"❌ {name}: API 失败 {e}")
            continue
        src = OUT / f"{name}.kv"
        src.write_text(code + "\n")
        try:
            stdout, stderr = run_kv(src)
        except subprocess.TimeoutExpired:
            print(f"❌ {name}: 运行超时（生成代码见 {src}）")
            continue
        got = [ln for ln in stdout.strip().splitlines() if ln.strip()]
        if got == expect:
            passed += 1
            print(f"✅ {name}")
        else:
            (OUT / f"{name}.fail.txt").write_text(
                f"task: {task}\nexpect: {expect}\ngot: {got}\nstderr: {stderr[-500:]}\n")
            print(f"❌ {name}: 期望 {expect}，得到 {got}（代码/详情见 {OUT}/{name}.*）")
    total = len(TASKS)
    print(f"\n══ 模型 {MODEL} 自适应正确率: {passed}/{total} = {passed * 100 // total}% ══")


if __name__ == "__main__":
    main()

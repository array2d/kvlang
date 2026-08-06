#!/usr/bin/env python3
"""agent_eval — 验证外部 LLM 以 README 为教学文档的 kvlang 自适应正确率。

用法:
  export KVLANG_EVAL_API_BASE=https://api.deepseek.com
  export KVLANG_EVAL_API_KEY=sk-...
  export KVLANG_EVAL_MODEL=qwen3.7-plus     # 可选，默认 qwen3.7-plus
  python3 tutorial/agent_eval.py

对每个任务：README + 任务描述 → LLM 生成 kvlang 代码 → 运行比对 stdout。
问题从 tutorial/questions/*.question 加载，答案在同名 .answer 中；结果保存于 /tmp/agent_eval/。
"""
from __future__ import annotations
import json, os, re, subprocess, sys, urllib.request, uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KV = str(ROOT / "kvlang")
OUT = Path("/tmp/agent_eval")
QUESTIONS_DIR = ROOT / "tutorial" / "questions"

API_BASE = os.environ.get("KVLANG_EVAL_API_BASE", "").rstrip("/")
API_KEY = os.environ.get("KVLANG_EVAL_API_KEY", "")
MODEL = os.environ.get("KVLANG_EVAL_MODEL", "qwen3.7-plus")

SYSTEM = """你是 kvlang 程序员。kvlang 是一门全新语言，下面的 README 是它的教学文档，语法以其中示例为准，不要套用其它语言的语法直觉。
只输出可直接运行的 kvlang 代码：顶层直接写语句或 rwfunc+调用；不要 markdown 围栏、不要解释文字。"""


def load_tasks():
    """从 tutorial/questions/*.question 加载 [README] 问题，同名 .answer 为期望输出。"""
    if QUESTIONS_DIR.is_dir():
        files = sorted(QUESTIONS_DIR.glob("*.question"))
        if files:
            tasks = []
            for f in files:
                name = f.stem
                desc = f.read_text().strip()
                if not desc.startswith("[README]"):
                    continue
                ans = QUESTIONS_DIR / f"{name}.answer"
                expect = ans.read_text().strip().splitlines() if ans.exists() else None
                tasks.append((name, desc, expect))
            return tasks
    return None


def chat(readme: str, task: str) -> str:
    sid = str(uuid.uuid4())
    req = urllib.request.Request(
        API_BASE + "/v1/chat/completions",
        data=json.dumps({
            "model": MODEL,
            "temperature": 0,
            "user": sid,
            "messages": [
                {"role": "system", "content": SYSTEM},
                {"role": "user", "content": f"# kvlang README（教学文档）\n\n{readme}\n\n---\n\n任务：{task}\n只输出 kvlang 代码。"},
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

    tasks = load_tasks()
    if not tasks:
        sys.exit(f"未找到 [README] 问题（{QUESTIONS_DIR}）")

    print(f"模型: {MODEL}")
    print(f"题目: {len(tasks)} 道\n")

    passed = 0
    for item in tasks:
        name, task, expect = item[0], item[1], item[2]
        try:
            response = chat(readme, task)
        except Exception as e:
            print(f"❌ {name}: API 失败 {e}")
            continue

        code = strip_fences(response)
        src = OUT / f"{name}.kv"
        src.write_text(code + "\n")
        try:
            stdout, stderr = run_kv(src)
        except subprocess.TimeoutExpired:
            print(f"❌ {name}: 运行超时（代码见 {src}）")
            continue
        errs = [ln for ln in stderr.strip().splitlines() if ln.strip() and not ln.startswith("info:") and not ln.startswith("warn:")]
        got = [ln for ln in stdout.strip().splitlines() if ln.strip()]
        if errs:
            msg = errs[0][:120]
            print(f"❌ {name}: 运行错误 — {msg}（代码见 {src}）")
            (OUT / f"{name}.fail.txt").write_text(f"task: {task}\nstderr:\n{stderr}\n")
        elif expect is not None and got == expect:
            passed += 1
            print(f"✅ {name}: {got}")
        elif expect is None and got:
            passed += 1
            print(f"✅ {name}: {got}")
        else:
            (OUT / f"{name}.fail.txt").write_text(f"task: {task}\nexpect: {expect}\ngot: {got}\nstderr: {stderr[-500:]}\n")
            print(f"❌ {name}: 期望 {expect}，得到 {got}（代码/详情见 {OUT}/{name}.*）")

    total = len(tasks)
    print(f"\n══ {MODEL} ══")
    print(f"正确率: {passed}/{total} = {passed * 100 // total}%")


if __name__ == "__main__":
    main()

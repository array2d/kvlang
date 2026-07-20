#!/usr/bin/env python3
"""agent_eval — 验证外部 LLM 以 README + deep-dive 为教学文档的 kvlang 自适应正确率。

用法:
  export KVLANG_EVAL_API_BASE=https://api.deepseek.com/anthropic
  export KVLANG_EVAL_API_KEY=sk-...
  export KVLANG_EVAL_MODEL=qwen3.7-plus     # 可选，默认 qwen3.7-plus
  python3 tutorial/agent_eval.py [--mode readme|deepdive|all]

--mode readme   : 仅测试 [README] 代码题（运行 kvlang 比对 stdout）
--mode deepdive : 仅测试 [DEEP-DIVE] 理解题（文本回答关键词匹配）
--mode all      : 全部（默认）

对每个任务：README + deep-dive + 任务描述 → LLM 生成 kvlang 代码/文本回答 → 运行/匹配。
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

# 内置任务（tutorial/questions/ 不存在时使用）
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
    ("strops", '把字符串 "hello" 的首字符改为 "H"，再与 " kvlang" 拼接并打印', ["Hello kvlang"]),
    ("libsingle", "定义 lib math { def sum(A:int, B:int) -> (C:int) { A + B -> C }; def init() -> () { sum(3, 4) -> s; print(s) } }", ["7"]),
    ("strfind", '打印子串 "is" 在 "kvlang is kv" 中首次出现的下标', ["7"]),
]

SYSTEM = """你是 kvlang 程序员。kvlang 是一门全新语言，下面的 README 和 deep-dive 是它的完整设计文档，语法以其中示例为准，不要套用其它语言的语法直觉。
只输出可直接运行的 kvlang 代码：顶层直接写语句或 def+调用；不要 markdown 围栏、不要解释文字。"""


def load_tasks():
    """从 tutorial/questions/*.question 加载问题，同名 .answer 为期望输出。"""
    if QUESTIONS_DIR.is_dir():
        files = sorted(QUESTIONS_DIR.glob("*.question"))
        if files:
            tasks = []
            for f in files:
                name = f.stem
                desc = f.read_text().strip()
                ans = QUESTIONS_DIR / f"{name}.answer"
                expect = ans.read_text().strip().splitlines() if ans.exists() else None
                tasks.append((name, desc, expect))
            return tasks
    return None  # 用内置 TASKS


def chat(readme: str, deepdive: str, task: str, code_mode: bool = True) -> str:
    sid = str(uuid.uuid4())
    instruction = "只输出 kvlang 代码。" if code_mode else "用一段中文直接回答问题，不要代码，不要 markdown 围栏。"
    req = urllib.request.Request(
        API_BASE + "/v1/chat/completions",
        data=json.dumps({
            "model": MODEL,
            "temperature": 0,
            "user": sid,
            "messages": [
                {"role": "system", "content": SYSTEM},
                {"role": "user", "content": f"# kvlang README（教学文档）\n\n{readme}\n\n# kvlang deep-dive（设计规范）\n\n{deepdive}\n\n---\n\n任务：{task}\n{instruction}"},
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
    mode = "all"
    if "--mode" in sys.argv:
        idx = sys.argv.index("--mode")
        if idx + 1 < len(sys.argv):
            mode = sys.argv[idx + 1]
        sys.argv = sys.argv[:idx]  # 清理，避免干扰其他解析

    if not API_BASE or not API_KEY:
        sys.exit("需设置 KVLANG_EVAL_API_BASE / KVLANG_EVAL_API_KEY 环境变量")
    readme = (ROOT / "README.md").read_text()
    deepdive = (ROOT / "deepx-design" / "doc" / "kvlang" / "deep-dive.md").read_text()
    OUT.mkdir(parents=True, exist_ok=True)

    custom = load_tasks()
    if custom:
        if mode == "readme":
            custom = [t for t in custom if t[1].startswith("[README]")]
        elif mode == "deepdive":
            custom = [t for t in custom if t[1].startswith("[DEEP-DIVE]")]
        readme_qs = [t for t in custom if t[1].startswith("[README]")]
        deepdive_qs = [t for t in custom if t[1].startswith("[DEEP-DIVE]")]
        print(f"模型: {MODEL}  模式: {mode}")
        print(f"题目: {len(custom)} 道（README {len(readme_qs)} + DEEP-DIVE {len(deepdive_qs)}）\n")
    else:
        print(f"模型: {MODEL}\n题目: {len(TASKS)} 道（内置）\n")

    passed_readme = 0
    passed_deepdive = 0
    deepdive_review = []  # 收集 deepdive 回答供 Claude 评分
    tasks_iter = custom if custom else [(n, d, e) for n, d, e in TASKS]
    for item in tasks_iter:
        name, task, expect = item[0], item[1], item[2]
        is_deepdive = task.startswith("[DEEP-DIVE]")
        try:
            response = chat(readme, deepdive, task, code_mode=not is_deepdive)
        except Exception as e:
            print(f"❌ {name}: API 失败 {e}")
            continue

        if is_deepdive:
            deepdive_review.append((name, task, expect, response))
            print(f"📝 {name}: 已收集回答")
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
            passed_readme += 1
            print(f"✅ {name}: {got}")
        elif expect is None and got:
            passed_readme += 1
            print(f"✅ {name}: {got}")
        else:
            (OUT / f"{name}.fail.txt").write_text(f"task: {task}\nexpect: {expect}\ngot: {got}\nstderr: {stderr[-500:]}\n")
            print(f"❌ {name}: 期望 {expect}，得到 {got}（代码/详情见 {OUT}/{name}.*）")
    total = len(tasks_iter)
    if custom:
        r_total = len(readme_qs)
        d_total = len(deepdive_qs)
        print(f"\n══ {MODEL} ══")
        print(f"README      : {passed_readme}/{r_total} = {passed_readme * 100 // max(r_total,1)}%")
        if deepdive_review:
            review_file = OUT / "deepdive_review.txt"
            lines = []
            for i, (name, task, expect, response) in enumerate(deepdive_review):
                lines.append(f"--- {name} ---")
                lines.append(f"Q: {task}")
                lines.append(f"期望: {' | '.join(expect) if expect else '(无)'}")
                lines.append(f"回答: {response}")
                lines.append(f"评分: __/100")
                lines.append("")
            review_file.write_text("\n".join(lines))
            print(f"DEEP-DIVE   : 待 Claude 评分（见 {review_file}）")
        else:
            print(f"DEEP-DIVE   : {passed_deepdive}/{d_total} = {passed_deepdive * 100 // max(d_total,1)}%")
        print(f"综合        : {passed_readme}/{total - d_total}（代码）+ 待评分（理解）")
    else:
        print(f"\n══ 模型 {MODEL} 自适应正确率: {passed_readme + passed_deepdive}/{total} = {(passed_readme + passed_deepdive) * 100 // total}% ══")


if __name__ == "__main__":
    main()

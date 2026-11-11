#!/usr/bin/env bash
# 下载 ABI 依赖（deps.json: repo → tag），安装到**系统最终位置**（不写 repo）：
#   头   → <prefix>/include/
#   前端 → <prefix>/lib/
#   后端 → <prefix>/lib/kvspace/（kvspace-durable）
#   可执行 → <prefix>/bin/
#
# 为什么装系统位置而不是 repo：repo 内的依赖副本是**二义来源**——同一个
# `include/kvspace/` 被两个消费者以相反要求使用（runtimec 的 stale-copy 闸要求它
# 不存在，冒烟探针又要求它存在）。副本一存在就要有人删、有人绕，改到最后没人说得清
# 哪份是权威。装到平台最终位置之后只有一份。
#
# 脚本自身不 sudo：CI 用 `sudo -E ./ci/deps.sh <prefix>` 调用，本地有权就直接跑。
#
# **prefix 是必填位置参数**：Linux 用 `/usr`，macOS 用 `/usr/local`（SIP 限制）。
# 没有默认值——默认一旦存在就会有人依赖它，装到哪就变成猜的了。
#
# 没有 kvspace-c：该仓已冻结，shm 引擎并入 kvspace 仓，随 libkvspace 一起发布。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${1:-}"
if [ -z "$PREFIX" ]; then
  echo "用法: $0 <prefix>（Linux: /usr；macOS: /usr/local）" >&2
  exit 2
fi
mkdir -p "$PREFIX/include" "$PREFIX/lib/kvspace" "$PREFIX/bin"

# 依赖清单用 python3 读，不用 jq：jq 不是必装项，缺了会让整段静默空转（见下）。
#
# **为什么把结果先取到变量再检查**：`set -e` 不捕获**命令替换**的失败——它只看
# `for` 语句本身的退出码。曾经写成 `for repo in $(jq -r 'keys[]' ...)`，缺 jq 时
# 命令替换失败、循环体一次都没跑，脚本却照样打印 ✅ 并 exit 0。那是 p1 级假成功：
# 什么都没装，报成功。所以每一次取值都 `|| { ...; exit 1; }`。
repos=$(python3 - "$ROOT/deps.json" <<'PY'
import json, sys
print("\n".join(json.load(open(sys.argv[1])).keys()))
PY
) || { echo "读取 deps.json 失败：$ROOT/deps.json" >&2; exit 1; }

for repo in $repos; do
  ver=$(python3 - "$ROOT/deps.json" "$repo" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))[sys.argv[2]])
PY
  ) || { echo "deps.json 里缺少 $repo 的版本" >&2; exit 1; }
  tmp="$(mktemp -d)"
  gh release download "$ver" -R "array2d/$repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ "$repo" = "kvspace-durable" ]; then
    # 后端单独放 <prefix>/lib/kvspace。
    cp -P "$tmp/lib/"*.so* "$PREFIX/lib/kvspace/"
  else
    # 其余（kvspace / blockmalloc / slotsboxmalloc）：头与库都直接落系统位置。
    # kvspace 的头落 <prefix>/include/kvspace/ 是**对的**——那是系统最终位置，
    # 不是 repo 副本；runtimec 的 stale-copy 闸查的是 `kvlang/include/kvspace`。
    if [ -d "$tmp/include" ]; then cp -r "$tmp/include/"* "$PREFIX/include/"; fi
    if [ -d "$tmp/lib" ]; then
      # blockmalloc / slotsboxmalloc are header-only (no shared libs).
      cp -P "$tmp/lib/"*.so* "$PREFIX/lib/"
    fi
  fi
  # 可选可执行（kvspace 的 CLI）→ bin/
  # 用 `install -m 755` 一步「拷 + 给权限」：**不能对 `$PREFIX/bin/*` 通配 chmod**——
  # prefix 是 `/usr` 时那会遍历整个系统 bin，撞上悬空链接（runner 的
  # `/usr/bin/grub-ntldr-img`）就 `set -e` 终止；即使不撞，也没有理由改系统里其它
  # 程序的权限。
  if [ -d "$tmp/bin" ]; then
    install -m 755 "$tmp/bin/"* "$PREFIX/bin/"
  fi
  rm -rf "$tmp"
done
echo "✅ ABI deps → $PREFIX/{include,lib,lib/kvspace,bin}"

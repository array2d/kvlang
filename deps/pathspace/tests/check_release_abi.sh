#!/usr/bin/env bash

set -u

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
say()  { printf '%s\n' "$*"; }
ok()   { printf "${GREEN}PASS${NC}  %s\n" "$*"; }
bad()  { printf "${RED}FAIL${NC}  %s\n" "$*"; }
warn() { printf "${YELLOW}WARN${NC}  %s\n" "$*"; }

if [ $# -ne 1 ]; then
    say "usage: $0 <unpacked-release-dir>" >&2
    exit 2
fi

RELEASE_DIR=$1
if [ ! -d "$RELEASE_DIR" ]; then
    bad "not a directory: $RELEASE_DIR"
    exit 2
fi

LIB_DIR="$RELEASE_DIR/lib"
INC_DIR="$RELEASE_DIR/include/pathspace"
SONAME_FILE="$LIB_DIR/libpathspace.so.1.0.0"

if ! ls "$LIB_DIR"/libpathspace.so* >/dev/null 2>&1 &&
   ls "$RELEASE_DIR"/lib/pathspace/libpathspace.so* >/dev/null 2>&1; then
    bad "$RELEASE_DIR 不是发布包布局（库在 lib/pathspace/ 子目录里，不在 lib/ 平铺）"
    say "      这看起来是 **cmake --install 的前缀目录**，不是 release tarball 解包后的目录。"
    say "      本检查器只验发布包布局：lib/libpathspace.so* + include/pathspace/。"
    say "      要验安装布局，先把它摊平再跑："
    say "        cp -P '$RELEASE_DIR/lib/pathspace/libpathspace.so'* '$RELEASE_DIR/lib/'"
    exit 2
fi

failures=0

say "── 1. lib/ 文件集合 ──"
if [ ! -d "$LIB_DIR" ]; then
    bad "missing directory: $LIB_DIR"
    failures=$((failures + 1))
else
    mapfile -t found < <(cd "$LIB_DIR" && ls -1 libpathspace.so* 2>/dev/null | sort)
    expected=(libpathspace.so libpathspace.so.1 libpathspace.so.1.0.0)
    missing=(); extra=()
    for e in "${expected[@]}"; do
        printf '%s\n' "${found[@]}" | grep -qx -- "$e" || missing+=("$e")
    done
    for f in "${found[@]}"; do
        printf '%s\n' "${expected[@]}" | grep -qx -- "$f" || extra+=("$f")
    done
    if [ ${#missing[@]} -eq 0 ] && [ ${#extra[@]} -eq 0 ]; then
        ok "lib/ 恰好 3 个：${expected[*]}"
    else
        bad "lib/ 文件集合不符（期望 3 个）"
        [ ${#missing[@]} -gt 0 ] && say "      缺少（${#missing[@]}）：${missing[*]}"
        [ ${#extra[@]} -gt 0 ]   && say "      多出（${#extra[@]}）：${extra[*]}"
        failures=$((failures + 1))
    fi
fi

say ""
say "── 2. 导出符号集合 ──"
SOURCE_TABLE="$(dirname "$0")/test_export_surface.c"
if [ ! -f "$SOURCE_TABLE" ]; then
    bad "找不到清单来源：$SOURCE_TABLE"
    failures=$((failures + 1))
    SOURCE_TABLE=""
fi

if [ ! -f "$SONAME_FILE" ]; then
    bad "找不到 $SONAME_FILE"
    failures=$((failures + 1))
elif [ -z "$SOURCE_TABLE" ]; then
    : # 上面已计失败
else
    mapfile -t expected_syms < <(
        grep -oE '\(const void \*\)&pathspace[A-Za-z0-9_]+' "$SOURCE_TABLE" |
        sed 's/.*&//' | sort -u
    )
    mapfile -t actual_syms < <(
        nm -D --defined-only "$SONAME_FILE" 2>/dev/null |
        awk '$2=="T"{print $3}' | grep '^pathspace' | sort -u
    )

    if [ ${#expected_syms[@]} -eq 0 ]; then
        bad "从 $SOURCE_TABLE 没解析出任何符号（格式变了？）"
        failures=$((failures + 1))
    else
        missing_syms=(); extra_syms=()
        while IFS= read -r s; do
            [ -z "$s" ] && continue
            printf '%s\n' "${actual_syms[@]}" | grep -qx -- "$s" || missing_syms+=("$s")
        done < <(printf '%s\n' "${expected_syms[@]}")
        while IFS= read -r s; do
            [ -z "$s" ] && continue
            printf '%s\n' "${expected_syms[@]}" | grep -qx -- "$s" || extra_syms+=("$s")
        done < <(printf '%s\n' "${actual_syms[@]}")

        if [ ${#missing_syms[@]} -eq 0 ] && [ ${#extra_syms[@]} -eq 0 ] &&
           [ ${#actual_syms[@]} -eq 16 ]; then
            ok "导出 T 符号恰好 16 个，与 test_export_surface.c 的表逐个同名"
        else
            bad "导出符号集合不符（实际 ${#actual_syms[@]} 个，期望 16）"
            [ ${#missing_syms[@]} -gt 0 ] && say "      库里缺少（${#missing_syms[@]}）：${missing_syms[*]}"
            [ ${#extra_syms[@]} -gt 0 ]   && say "      库里多出（${#extra_syms[@]}）：${extra_syms[*]}"
            failures=$((failures + 1))
        fi
    fi

    say ""
    say "── 3. 内部符号泄漏 ──"
    leak_count=$(nm -D --defined-only "$SONAME_FILE" 2>/dev/null |
                 awk '$2=="T"{print $3}' | grep -c '^pathspaceShm')
    if [ "$leak_count" -eq 0 ]; then
        ok "pathspaceShm* 泄漏 0"
    else
        bad "pathspaceShm* 泄漏 $leak_count 个（内部符号不得导出）"
        nm -D --defined-only "$SONAME_FILE" 2>/dev/null |
            awk '$2=="T"{print $3}' | grep '^pathspaceShm' | sed 's/^/      /'
        failures=$((failures + 1))
    fi
fi

say ""
say "── 4. 头文件 ──"
if [ ! -d "$INC_DIR" ]; then
    bad "missing directory: $INC_DIR"
    failures=$((failures + 1))
else
    mapfile -t heads < <(cd "$INC_DIR" && ls -1 *.h 2>/dev/null | sort)
    expected_heads=(const.h pathspace.h pathspace_xvalue.h)
    hmissing=(); hextra=()
    for e in "${expected_heads[@]}"; do
        printf '%s\n' "${heads[@]}" | grep -qx -- "$e" || hmissing+=("$e")
    done
    for f in "${heads[@]}"; do
        printf '%s\n' "${expected_heads[@]}" | grep -qx -- "$f" || hextra+=("$f")
    done
    if [ ${#hmissing[@]} -eq 0 ] && [ ${#hextra[@]} -eq 0 ]; then
        ok "include/pathspace/ 恰好 3 个头：${expected_heads[*]}"
    else
        bad "include/pathspace/ 头文件集合不符"
        [ ${#hmissing[@]} -gt 0 ] && say "      缺少：${hmissing[*]}"
        [ ${#hextra[@]} -gt 0 ]   && say "      多出：${hextra[*]}"
        failures=$((failures + 1))
    fi

    probe=$(mktemp /tmp/release_abi_probe_XXXXXX.c)
    cat > "$probe" <<'PROBE'
int main(void) { return 0; }
PROBE
    if cc -std=c11 -Wall -Wextra -Werror -fsyntax-only \
          -I "$RELEASE_DIR/include" "$probe" 2>/tmp/release_abi_probe.err; then
        ok "三头齐 include 编译通过"
    else
        bad "三头齐 include 编译失败"
        sed 's/^/      /' /tmp/release_abi_probe.err | head -10
        failures=$((failures + 1))
    fi
    rm -f "$probe" /tmp/release_abi_probe.err

    if [ -f "$SONAME_FILE" ]; then
        decl_probe=$(mktemp /tmp/release_abi_decl_XXXXXX.c)
        {
            echo '#include <pathspace/const.h>'
            echo '#include <pathspace/pathspace.h>'
            echo '#include <pathspace/pathspace_xvalue.h>'
            echo 'static const void *const probe_table[] = {'
            grep -hoE '\bpathspace[A-Za-z0-9_]+ *\(' "$INC_DIR"/*.h |
                sed -E 's/ *\($//' | sort -u |
                while IFS= read -r sym; do
                    echo "    (const void *)&$sym,"
                done
            echo '};'
            echo 'int main(void){ (void)probe_table; return 0; }'
        } > "$decl_probe"

        if cc -std=c11 -fsyntax-only -I "$RELEASE_DIR/include" "$decl_probe" \
                2>/tmp/release_abi_decl.err; then
            decl_obj=$(mktemp /tmp/release_abi_decl_XXXXXX.o)
            if cc -std=c11 -c -I "$RELEASE_DIR/include" "$decl_probe" \
                    -o "$decl_obj" 2>/dev/null &&
               cc "$decl_obj" -o /dev/null -L "$LIB_DIR" -lpathspace \
                    -Wl,-rpath,"$LIB_DIR" 2>/tmp/release_abi_link.err; then
                ok "头里声明的函数全部能在 .so 里链接到"
            else
                bad "头里有声明、库里链接不到（能编过、一链接就失败）"
                grep -oE "undefined reference to \`[^']+'" /tmp/release_abi_link.err |
                    sort -u | sed 's/^/      /' | head -10
                failures=$((failures + 1))
            fi
            rm -f "$decl_obj" /tmp/release_abi_link.err
        else
            bad "按头文件声明构造的探针无法编译"
            sed 's/^/      /' /tmp/release_abi_decl.err | head -10
            failures=$((failures + 1))
        fi
        rm -f "$decl_probe" /tmp/release_abi_decl.err
    fi
fi

say ""
if [ "$failures" -eq 0 ]; then
    printf "${GREEN}══ release ABI 检查通过 ══${NC}\n"
    exit 0
fi
printf "${RED}══ release ABI 检查失败：%d 组 ══${NC}\n" "$failures"
exit 1

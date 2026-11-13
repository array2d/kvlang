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
    bad "$RELEASE_DIR is not in release-tarball layout (libraries found in lib/pathspace/ instead of flat lib/)"
    say "      This looks like a cmake --install prefix rather than an unpacked release tarball."
    say "      This checker expects: lib/libpathspace.so* + include/pathspace/."
    say "      To check an installed prefix, flatten it first:"
    say "        cp -P '$RELEASE_DIR/lib/pathspace/libpathspace.so'* '$RELEASE_DIR/lib/'"
    exit 2
fi

failures=0

say "-- 1. lib/ file set --"
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
        ok "lib/ contains exact 3 files: ${expected[*]}"
    else
        bad "lib/ file set mismatch (expected 3 files)"
        [ ${#missing[@]} -gt 0 ] && say "      missing (${#missing[@]}): ${missing[*]}"
        [ ${#extra[@]} -gt 0 ]   && say "      extra (${#extra[@]}): ${extra[*]}"
        failures=$((failures + 1))
    fi
fi

say ""
say "-- 2. Exported symbol set --"
SOURCE_TABLE="$(dirname "$0")/test_export_surface.c"
if [ ! -f "$SOURCE_TABLE" ]; then
    bad "symbol table source not found: $SOURCE_TABLE"
    failures=$((failures + 1))
    SOURCE_TABLE=""
fi

if [ ! -f "$SONAME_FILE" ]; then
    bad "not found: $SONAME_FILE"
    failures=$((failures + 1))
elif [ -z "$SOURCE_TABLE" ]; then
    : # already counted above
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
        bad "no symbols parsed from $SOURCE_TABLE (format changed?)"
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
            ok "exported T symbols match exact 16 entries in test_export_surface.c"
        else
            bad "exported symbol set mismatch (actual ${#actual_syms[@]}, expected 16)"
            [ ${#missing_syms[@]} -gt 0 ] && say "      missing (${#missing_syms[@]}): ${missing_syms[*]}"
            [ ${#extra_syms[@]} -gt 0 ]   && say "      extra (${#extra_syms[@]}): ${extra_syms[*]}"
            failures=$((failures + 1))
        fi
    fi

    say ""
    say "-- 3. Internal symbol leakage --"
    leak_count=$(nm -D --defined-only "$SONAME_FILE" 2>/dev/null |
                 awk '$2=="T"{print $3}' | grep -c '^pathspaceShm')
    if [ "$leak_count" -eq 0 ]; then
        ok "pathspaceShm* leaked symbols: 0"
    else
        bad "pathspaceShm* leaked $leak_count internal symbols"
        nm -D --defined-only "$SONAME_FILE" 2>/dev/null |
            awk '$2=="T"{print $3}' | grep '^pathspaceShm' | sed 's/^/      /'
        failures=$((failures + 1))
    fi
fi

say ""
say "-- 4. Header files --"
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
        ok "include/pathspace/ contains exact 3 headers: ${expected_heads[*]}"
    else
        bad "include/pathspace/ header set mismatch"
        [ ${#hmissing[@]} -gt 0 ] && say "      missing: ${hmissing[*]}"
        [ ${#hextra[@]} -gt 0 ]   && say "      extra: ${hextra[*]}"
        failures=$((failures + 1))
    fi

    probe=$(mktemp /tmp/release_abi_probe_XXXXXX.c)
    cat > "$probe" <<'PROBE'
int main(void) { return 0; }
PROBE
    if cc -std=c11 -Wall -Wextra -Werror -fsyntax-only \
          -I "$RELEASE_DIR/include" "$probe" 2>/tmp/release_abi_probe.err; then
        ok "header syntax probe compiled cleanly"
    else
        bad "header syntax probe failed to compile"
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
                ok "all functions declared in headers resolve in .so"
            else
                bad "functions declared in headers missing from .so at link time"
                grep -oE "undefined reference to \`[^']+'" /tmp/release_abi_link.err |
                    sort -u | sed 's/^/      /' | head -10
                failures=$((failures + 1))
            fi
            rm -f "$decl_obj" /tmp/release_abi_link.err
        else
            bad "declaration probe failed to compile"
            sed 's/^/      /' /tmp/release_abi_decl.err | head -10
            failures=$((failures + 1))
        fi
        rm -f "$decl_probe" /tmp/release_abi_decl.err
    fi
fi

say ""
if [ "$failures" -eq 0 ]; then
    printf "${GREEN}== release ABI check passed ==${NC}\n"
    exit 0
fi
printf "${RED}== release ABI check failed: %d group(s) ==${NC}\n" "$failures"
exit 1

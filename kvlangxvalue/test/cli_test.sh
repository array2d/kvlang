
set -u

CLI=$1
STORE=$2
failures=0

export KVSPACE="shm://$STORE"
rm -f "$STORE"

check_output() {
    description=$1
    expected=$2
    actual=$3
    if [ "$actual" != "$expected" ]; then
        echo "  FAIL $description: got '$actual', want '$expected'"
        failures=$((failures + 1))
    fi
}

check_exit() {
    description=$1
    expected=$2
    actual=$3
    if [ "$actual" -ne "$expected" ]; then
        echo "  FAIL $description: exit $actual, want $expected"
        failures=$((failures + 1))
    fi
}

"$CLI" newT /int64 int64 42 >/dev/null
check_output "int64" "int64	42" "$("$CLI" readT /int64)"

"$CLI" newT /float64 float64 3.140 >/dev/null
check_output "float64 归一化" "float64	3.14" "$("$CLI" readT /float64)"

"$CLI" newT /bool bool true >/dev/null
check_output "bool" "bool	true" "$("$CLI" readT /bool)"

"$CLI" newT /utf8 '[]char/utf8' hello >/dev/null
check_output "utf8" "[]char/utf8	hello" "$("$CLI" readT /utf8)"

"$CLI" newT /utf32 '[]char/utf32' 中a >/dev/null
check_output "utf32" "[]char/utf32	中a" "$("$CLI" readT /utf32)"

"$CLI" writeT /int64 123456 >/dev/null
check_output "writeT 沿用 int64" "int64	123456" "$("$CLI" readT /int64)"

"$CLI" newT /bad int8 300 >/dev/null 2>&1
check_exit "int8 越界" 1 $?

"$CLI" newT /bad bool 1 >/dev/null 2>&1
check_exit "bool 非法表示" 1 $?

"$CLI" newT /ptr '*int64' /x >/dev/null 2>&1
check_exit "Ptr 无文本形态" 1 $?

"$CLI" readT /missing >/dev/null 2>&1
check_exit "key 不存在" 3 $?

"$CLI" bogus >/dev/null 2>&1
check_exit "未知命令" 2 $?

"$CLI" newT /list/b int8 1 >/dev/null
"$CLI" newT /list/a int8 1 >/dev/null
"$CLI" newT /list/'[10]' int8 1 >/dev/null
"$CLI" newT /list/'[2]' int8 1 >/dev/null
check_output "list 排序" "$(printf '[2]\n[10]\na\nb')" "$("$CLI" list /list/)"

"$CLI" newT /lib/demo·f '[]char/utf8' hello >/dev/null
check_output "print 文本 body" \
    "$(printf 'langtype=[]char/utf8\nbodylen=5\nhello')" \
    "$("$CLI" print /lib/demo·f)"

"$CLI" newT /bin int64 42 >/dev/null
bin_first=$("$CLI" print /bin | sed -n '3p')
case $bin_first in
00000000\ 2a*) ;;
*)
    echo "  FAIL print 二进制 body: got '$bin_first'"
    failures=$((failures + 1))
    ;;
esac

head_line=$("$CLI" head /int64)
case $head_line in
bodylen=8*) ;;
*)
    echo "  FAIL head: got '$head_line', want bodylen=8"
    failures=$((failures + 1))
    ;;
esac

rm -f "$STORE"
if [ "$failures" -ne 0 ]; then
    echo "cli_test: $failures failure(s)"
    exit 1
fi
echo "cli_test: ok"

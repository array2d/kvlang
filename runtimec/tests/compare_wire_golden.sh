#!/bin/sh
# 比对替身与真库的 wire 金标准，逐字节。
#
#   tests/compare_wire_golden.sh <left-golden-binary> <right-golden-binary>
#
# 两者必须是**同一批构造用例**的产出（同一个 xvalue_wire_golden.c，只有链接
# 对象不同：一个链 tests/fake_kvspace.c、一个链真 libkvspace）。
#
# 只比字节，不解释字节——解释会引入第二套「什么算合法」的判断，而这里要验的
# 恰恰是两边对同一份 head 定义的理解一致。
#
# 验的是「替身不漂移」：替身是对真实实现的照抄，抄的那一刻对，漂移时不报错。
# 2026-11-03 就发生过一次（替身的 new_slack 还在造 8 字节头），当时的红被误
# 认成产品 bug。
set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <left-golden-binary> <right-golden-binary>" >&2
    exit 2
fi

left_bin=$1
right_bin=$2

# 退出码必须直连：管道只用于展示，取 $? 会拿到管道末端程序的退出码。
left_out=$("$left_bin")
left_status=$?
right_out=$("$right_bin")
right_status=$?

if [ "$left_status" -ne 0 ] || [ "$right_status" -ne 0 ]; then
    echo "wire golden: a golden binary failed (left=$left_status right=$right_status)" >&2
    exit 1
fi

if [ "$left_out" = "$right_out" ]; then
    echo "wire golden: identical ($(printf '%s\n' "$left_out" | wc -l) cases)"
    exit 0
fi

echo "wire golden: MISMATCH" >&2
printf '%s\n' "$left_out" > /tmp/rt-wire-golden-left.txt
printf '%s\n' "$right_out" > /tmp/rt-wire-golden-right.txt
diff -u /tmp/rt-wire-golden-left.txt /tmp/rt-wire-golden-right.txt >&2 || true
exit 1

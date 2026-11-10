#!/bin/sh
# 比对两个 wire 金标准输出，逐字节。
#
#   tests/compare_wire_golden.sh <left-golden-binary> <right-golden-binary>
#
# 两者必须是**同一批操作**的产出（同一组用例、同一顺序），只有实现不同。逐字节
# 相等证明的是「两边对同一份 head 定义的理解一致」——这是跨语言共用一个 wire 格式
# 唯一的硬证据。
#
# 为什么在本仓（kvlang）：产出源 `tests/xvalue_wire_golden.c` 在这里，构造器
# （`kvlangXvalueHeadNew*`）也在这里。kvspace 只留字节层，它提供的是「读侧解释器」，
# 两边的对照就是「本仓造的字节 vs kvspace 字节层读出来的字节」。
#
# 只比字节，不解释字节——解释会引入第二套「什么算合法」的判断，而这里要验的恰恰
# 是两边对同一份定义的理解一致。
set -eu

left_bin=$1
right_bin=$2

left_out=$("$left_bin")
right_out=$("$right_bin")

if [ "$left_out" = "$right_out" ]; then
    echo "wire golden: identical ($(printf '%s\n' "$left_out" | wc -l) cases)"
    exit 0
fi

echo "wire golden: MISMATCH" >&2
left_tmp=$(mktemp)
right_tmp=$(mktemp)
printf '%s\n' "$left_out" > "$left_tmp"
printf '%s\n' "$right_out" > "$right_tmp"
diff -u "$left_tmp" "$right_tmp" >&2 || true
rm -f "$left_tmp" "$right_tmp"
exit 1

package builtin

import (
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

// isRelative 判断参数是否为显式相对路径（./ 开头）。
func isRelative(param string) bool {
	return len(param) >= 2 && param[:2] == "./"
}

// isAbsolute 判断参数是否为绝对路径（/ 开头）。
func isAbsolute(param string) bool {
	return len(param) > 0 && param[0] == '/'
}

// isImmediateNumber 判断参数是否为数字字面量（整数或浮点）。
// scanner 只对纯数字序列产生 Literal token，不会有前缀符号。
func isImmediateNumber(param string) bool {
	if len(param) == 0 {
		return false
	}
	for _, c := range param {
		if c >= '0' && c <= '9' || c == '.' || c == 'e' || c == 'E' {
			continue
		}
		return false
	}
	return true
}

// resolveWriteKey 将写槽参数解析为 kvspace 绝对 key。
// ./x  → /vthread/<vtid>/x（显式相对）
// /abs → /abs（绝对路径，原样）
// x    → /vthread/<vtid>/x（裸 ident，与 ./x 等价）
func resolveWriteKey(vtid, param string) string {
	if isRelative(param) {
		return keytree.VThreadAt(vtid, param[2:])
	}
	if isAbsolute(param) {
		return param
	}
	// 裸标识符：视为本帧局部变量，等价于 ./param
	return keytree.VThreadAt(vtid, param)
}

// ResolveReadValue 将读槽参数解析为实际值（导出，供 kvcpu 等包使用）。
// resolveReadValue 是内部别名。
func ResolveReadValue(kv kvspace.KVSpace, vtid, param string) string {
	return resolveReadValue(kv, vtid, param)
}

// resolveReadValue 将读槽参数解析为实际值。
//
// 规则（无歧义，无回退）：
//   ./x  → 本帧槽（显式相对路径）
//   /abs → 全局槽（绝对路径）
//   3    → 数字字面量（以数字开头）
//   "x"  → 字符串字面量（scanner 已剥引号存为 Literal，不会走到这里作为 Ident）
//   x    → 本帧局部变量（字面字符串必须用引号，裸 ident 唯一含义是本帧槽）
func resolveReadValue(kv kvspace.KVSpace, vtid, param string) string {
	if isRelative(param) {
		// ./x → /vthread/<vtid>/x
		val, _ := kv.Get(keytree.VThreadAt(vtid, param[2:]))
		return val
	}
	if isAbsolute(param) {
		// /abs/path → 绝对路径直接查
		val, _ := kv.Get(param)
		return val
	}
	if isImmediateNumber(param) {
		// 3, 4.5, -1 → 数字字面量，无需 KV 查找
		return param
	}
	// 裸 ident → 本帧局部变量（与 ./param 完全等价）
	val, _ := kv.Get(keytree.VThreadAt(vtid, param))
	return val
}

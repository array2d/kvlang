package builtin

import "kvlang/internal/kvspace"

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

// isImmediateBool 判断参数是否为布尔字面量。
func isImmediateBool(param string) bool { return param == "true" || param == "false" }

// resolveWriteKey 将写槽参数解析为 kvspace 绝对 key。
//
//	./x  → framePath/x（帧局部变量，显式相对）
//	/abs → /abs（绝对路径，原样）
//	x    → framePath/x（裸 ident，与 ./x 等价）
//
// framePath 为当前帧根（keytree.FrameRoot(pc)）。
func resolveWriteKey(framePath, param string) string {
	if isRelative(param) {
		return framePath + "/" + param[2:]
	}
	if isAbsolute(param) {
		return param
	}
	return framePath + "/" + param
}

// ResolveReadValue 将读槽参数解析为实际值（导出，供 kvcpu 等包使用）。
func ResolveReadValue(kv kvspace.KVSpace, framePath, param string) string {
	return resolveReadValue(kv, framePath, param)
}

// resolveReadValue 将读槽参数解析为实际值。
//
// 规则（无歧义，无回退）：
//
//	"X = → "X ="               — 引号字符串字面量（parser 写入 " 前缀）
//	./x  → kv.Get(framePath/x)  — 帧局部变量（显式相对路径）
//	/abs → kv.Get(/abs)          — 全局绝对路径
//	3    → "3"                   — 数字字面量，无 KV 查找
//	x    → kv.Get(framePath/x)  — 裸 ident，等价于 ./x
//
// framePath 为当前帧根（keytree.FrameRoot(pc)），由 Link 模型确保
// framePath 本身不是链接节点，故读取不会穿透到函数模板。
func resolveReadValue(kv kvspace.KVSpace, framePath, param string) string {
	if len(param) > 0 && param[0] == '"' {
		return param[1:] // 引号字符串字面量：去掉 " 前缀，直接返回
	}
	if isRelative(param) {
		val, _ := kv.Get(framePath + "/" + param[2:])
		return val.Str()
	}
	if isAbsolute(param) {
		val, _ := kv.Get(param)
		return val.Str()
	}
	if isImmediateNumber(param) || isImmediateBool(param) {
		return param
	}
	val, _ := kv.Get(framePath + "/" + param)
	return val.Str()
}

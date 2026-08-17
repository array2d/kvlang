package builtin

import (
	"fmt"
)

// 数值 kind 集合（按位宽升序）。
var (
	signedIntKinds     = []string{"int8", "int16", "int32", "int64"}
	allIntKinds        = []string{"int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64"}
	floatKinds         = []string{"float32", "float64"}
	signedOrFloatKinds = []string{"int8", "int16", "int32", "int64", "float32", "float64"}
	numKinds           = []string{"int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float32", "float64"}
)

// registerKinds 将 op 注册到指定 kind 集合，opcode = kind.op（如 int64.add、float64.sqrt）。
// arity 1/2；ret 为返回类型（空串 = 与输入同 kind）。
func registerKinds(op string, arity int, ret string, kinds []string, impl Rwir) {
	for _, k := range kinds {
		r := ret
		if r == "" {
			r = k
		}
		sig := fmt.Sprintf("rwir %s(A:%s) -> (C:%s)", op, k, r)
		if arity == 2 {
			sig = fmt.Sprintf("rwir %s(A:%s, B:%s) -> (C:%s)", op, k, k, r)
		}
		Register(k+"."+op, sig, impl)
	}
	// 裸 op + 字形回退：非数值输入（字符串/未知类型）走此路径 → TypeError 或拼接/比较
	r := ret
	if r == "" {
		r = "any"
	}
	sig := fmt.Sprintf("rwir %s(A:any) -> (C:%s)", op, r)
	if arity == 2 {
		sig = fmt.Sprintf("rwir %s(A:any, B:any) -> (C:%s)", op, r)
	}
	registerWord(op, sig, impl)
}

// NumOp 判断 opcode 是否为多态数值 op（供 lower 特化）。
func NumOp(opcode string) bool {
	switch opcode {
	case "add", "sub", "mul", "div", "neg", "mod",
		"bitand", "bitor", "bitxor", "shl", "shr",
		"eq", "neq", "lt", "le", "gt", "ge",
		"pow", "sqrt", "exp", "log",
		"abs", "sign", "max", "min":
		return true
	}
	return false
}

// IsNumKind 判断 kind 字符串是否为数值。
func IsNumKind(k string) bool { return isIntKind(k) || isFloatKind(k) }

// WiderNumKind 返回两个数值 kind 中更宽者（int 按位宽，float 比 int 宽，float64 比 float32 宽）。
func WiderNumKind(ak, bk string) string {
	if isIntKind(ak) && isIntKind(bk) {
		return WiderIntKind(ak, bk)
	}
	if isFloatKind(ak) && isFloatKind(bk) {
		return WiderFloatKind(ak, bk)
	}
	if isFloatKind(ak) {
		return ak
	}
	return bk
}

// OpKind 返回 op 在给定输入 kind 下的注册 kind（"" 表示该 kind 无此 op）。
// 规则：uint 无 neg/abs；float 无 mod；int/uint 无 pow/sqrt/exp/log（int 提升为 float64）。
func OpKind(op, k string) string {
	if !IsNumKind(k) {
		return ""
	}
	switch op {
	case "pow", "sqrt", "exp", "log":
		if isFloatKind(k) {
			return k
		}
		return "float64"
	case "mod":
		if isIntKind(k) {
			return k
		}
		return ""
	case "bitand", "bitor", "bitxor", "shl", "shr":
		if isIntKind(k) {
			if isUnsignedKind(k) {
				return "uint64"
			}
			return "int64"
		}
		return ""
	case "neg", "abs":
		if isUnsignedKind(k) {
			return ""
		}
		return k
	default:
		return k
	}
}

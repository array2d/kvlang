package rwir

import (
	"strconv"
	"strings"
)

// 签名类型表达式（runtime篇-07，修订：无数值家族）：语法校验 + 值匹配。
//
//	type   = atom ("|" atom)*
//	atom   = [dims] ( any | char | kind )
//	dims   = "[]" | "[" dim ("," dim)* "]"
//	dim    = integer | "?"
//	any    = "any"           # 通配，匹配任意 kind
//	char   = "char"          # char/ 前缀（utf32/utf8/ascii）
//	kind   = 精确 kind 串    # 见 knownKinds
//
// 铁律：不提供 int/uint/float/num 数值家族——位宽是开放集合（int4/fp8/fp16…），
// 封闭枚举会漏、开放前缀会收进 runtime 尚不支持的 kind。多态靠显式 "|" 枚举。

func isCharKind(k string) bool { return strings.HasPrefix(k, "char/") }

// 精确 kind 集合（对齐 runtime kind 常量，不含 None）。
var knownKinds = map[string]bool{
	"bool": true,
	"int8": true, "int16": true, "int32": true, "int64": true,
	"uint8": true, "uint16": true, "uint32": true, "uint64": true,
	"float32": true, "float64": true,
	"char/utf32": true, "char/utf8": true, "char/ascii": true,
	"dict": true, "index": true, "extindex": true,
	"rwir": true, "rwfunc": true, "scope": true, "time": true, "duration": true,
}

func validBase(s string) bool {
	if s == "" {
		return false
	}
	if s == "any" || s == "char" {
		return true
	}
	return knownKinds[s]
}

func validDim(s string) bool {
	if s == "" {
		return false
	}
	if s == "?" {
		return true
	}
	for i := 0; i < len(s); i++ {
		if s[i] < '0' || s[i] > '9' {
			return false
		}
	}
	return true
}

func validDims(s string) bool {
	if s == "" {
		return true
	}
	for _, d := range strings.Split(s, ",") {
		if !validDim(d) {
			return false
		}
	}
	return true
}

func validAtom(s string) bool {
	if s == "" {
		return false
	}
	if s[0] == '[' {
		end := strings.IndexByte(s, ']')
		if end < 0 {
			return false
		}
		if !validDims(s[1:end]) {
			return false
		}
		base := s[end+1:]
		return base != "" && validBase(base)
	}
	return validBase(s)
}

// ValidTypeExpr 判断类型表达式字符串是否语法合法（装载期校验）。
func ValidTypeExpr(expr string) bool {
	if expr == "" {
		return false
	}
	for _, atom := range strings.Split(expr, "|") {
		if !validAtom(atom) {
			return false
		}
	}
	return true
}

func baseMatch(s, kind string) bool {
	switch s {
	case "any":
		return true
	case "char":
		return isCharKind(kind)
	default:
		return s == kind
	}
}

func matchShape(shape string, ndim int, dims []int32) bool {
	if shape == "" {
		return ndim >= 1
	}
	parts := strings.Split(shape, ",")
	if len(parts) != ndim {
		return false
	}
	for i, p := range parts {
		if p == "?" {
			continue
		}
		n, err := strconv.ParseInt(p, 10, 32)
		if err != nil || int32(n) != dims[i] {
			return false
		}
	}
	return true
}

// matchAtom：ndim = -1 表示「已消费 dims，不再判 ndim」（递归哨兵）。
func matchAtom(atom, kind string, ndim int, dims []int32) bool {
	if strings.HasPrefix(atom, "[") {
		end := strings.IndexByte(atom, ']')
		if end < 0 {
			return false
		}
		if !matchShape(atom[1:end], ndim, dims) {
			return false
		}
		return matchAtom(atom[end+1:], kind, -1, nil)
	}
	if ndim >= 0 && ndim != 0 {
		return false
	}
	return baseMatch(atom, kind)
}

// MatchType 判断值（kind/ndim/dims）是否匹配类型表达式：任一 atom 命中即 true。
func MatchType(expr, kind string, ndim int, dims []int32) bool {
	if expr == "" {
		return false
	}
	for _, atom := range strings.Split(expr, "|") {
		if matchAtom(atom, kind, ndim, dims) {
			return true
		}
	}
	return false
}

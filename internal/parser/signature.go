package parser

import (
	"strings"
)

// FormalParams 表示函数签名的形参列表。
type FormalParams struct {
	Reads  []string
	Writes []string
}

// ParseSignature 解析函数签名，提取形参。
func ParseSignature(sig string) FormalParams {
	var fp FormalParams
	sig = strings.TrimSpace(sig)
	if strings.HasPrefix(sig, "def ") {
		sig = strings.TrimSpace(sig[4:])
	}
	if len(sig) >= 2 && sig[0] == '(' && sig[len(sig)-1] == ')' {
		sig = sig[1 : len(sig)-1]
	}
	arrow := strings.Index(sig, "->")
	if arrow < 0 {
		return fp
	}
	left := strings.TrimSpace(sig[:arrow])
	right := strings.TrimSpace(sig[arrow+2:])
	if lp := strings.Index(left, "("); lp >= 0 {
		rp := strings.LastIndex(left, ")")
		if rp > lp {
			fp.Reads = extractParamNames(left[lp+1 : rp])
		}
	}
	right = strings.TrimSpace(right)
	if len(right) >= 2 && right[0] == '(' && right[len(right)-1] == ')' {
		fp.Writes = extractParamNames(right[1 : len(right)-1])
	} else {
		fp.Writes = extractParamNames(right)
	}
	return fp
}

func extractParamNames(s string) []string {
	var names []string
	for _, p := range strings.Split(s, ",") {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		if colon := strings.Index(p, ":"); colon >= 0 {
			p = p[:colon]
		}
		p = strings.TrimSpace(p)
		if p != "" {
			names = append(names, p)
		}
	}
	return names
}

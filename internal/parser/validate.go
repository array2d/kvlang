package parser

import (
	"fmt"
	"strings"
)

// isKeyRef 判断是否为 KV 路径引用 (以 / 或 ./ 开头)。
func isKeyRef(s string) bool {
	return strings.HasPrefix(s, "/") || strings.HasPrefix(s, "./")
}

// isString 判断参数是否为双引号字符串字面量 (e.g. "f32", "[128]")。
func isString(s string) bool {
	return len(s) >= 2 && s[0] == '"' && s[len(s)-1] == '"'
}

func validateRef(raw, line string) error {
	if isString(raw) || (len(raw) >= 2 && raw[0] == '\'' && raw[len(raw)-1] == '\'') {
		return nil // 已加引号，合法
	}
	// 裸路径在 reads 位置允许（如 ./tmp + 1 -> './Y'）
	if isKeyRef(raw) {
		return nil
	}
	return nil
}

func validateKeyRefs(rawExpr, role, line string) error {
	for _, raw := range parseParamListRaw(rawExpr) {
		if isString(raw) || (len(raw) >= 2 && raw[0] == '\'' && raw[len(raw)-1] == '\'') {
			continue
		}
		unquoted := stripQuotes(raw)
		if isKeyRef(unquoted) {
			return fmt.Errorf("%s %q must be single-quoted (e.g. %s) in: %s", role, unquoted, "'"+unquoted+"'", line)
		}
	}
	return nil
}

package builtin

import (
	"strings"

	"kvlang/internal/logx"
)

func evalPrint(inputs []nativeValue) (nativeValue, error) {
	if len(inputs) == 0 {
		return nativeValue{kind: "string", raw: ""}, nil
	}
	parts := make([]string, len(inputs))
	for i, v := range inputs {
		parts[i] = v.String()
	}
	logx.Debug("PRINT %s", strings.Join(parts, " "))
	return nativeValue{kind: "string", raw: strings.Join(parts, " ")}, nil
}

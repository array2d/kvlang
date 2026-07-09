package builtin

import "fmt"

import (
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type mOp struct{ kind string }
func (o mOp) Call(f *op.Frame) error {
	r, err := evalMath(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalMath(kind string, inputs []nativeValue) (nativeValue, error) {
	switch kind {
	case "abs": return evalAbs(inputs)
	case "pow": return evalPow(inputs)
	case "min": return evalMin(inputs)
	case "max": return evalMax(inputs)
	case "sqrt": return evalSqrt(inputs)
	case "exp": return evalExp(inputs)
	case "log": return evalLog(inputs)
	case "neg": return evalUnaryArith(inputs, func(a float64) float64 { return -a })
	case "sign": return evalSign(inputs)
	default: return nativeValue{}, fmt.Errorf("unknown math: %s", kind)
	}
}

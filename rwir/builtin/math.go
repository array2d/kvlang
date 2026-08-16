package builtin

import (
	"fmt"
	"math"

	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	Register("pow",  "rwir pow(A:num, B:num) -> (C:float64)", mOp{kind: "pow"})
	registerWord("sqrt", "rwir sqrt(A:num) -> (C:float64)", mOp{kind: "sqrt"})
	Register("exp",  "rwir exp(A:num) -> (C:float64)",        mOp{kind: "exp"})
	Register("log",  "rwir log(A:num) -> (C:float64)",        mOp{kind: "log"})
	Register("neg",  "rwir neg(A:num) -> (C:num)",            mOp{kind: "neg"})
}

type mOp struct{ kind string }
func (o mOp) Call(f *rwir.Frame) error {
	r, err := evalMath(o.kind, readInputs(f))
	if err != nil { vthread.SetError(bg, f.KV, f.Vtid, f.PC, err.Error()); return err }
	return writeResult(f, r)
}

func evalMath(kind string, inputs []kvspace.XValue) (kvspace.XValue, error) {
	switch kind {
	case "pow":  return evalPow(inputs)
	case "sqrt": return evalSqrt(inputs)
	case "exp":  return evalExp(inputs)
	case "log":  return evalLog(inputs)
	case "neg":
		if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
		return evalNeg(inputs[0])
	default:     return kvspace.None{}, fmt.Errorf("unknown math: %s", kind)
	}
}

func evalPow(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireBinary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0], inputs[1]); err != nil { return kvspace.None{}, err }
	return kvspace.NewFloat64(math.Pow(asFloat(inputs[0]), asFloat(inputs[1]))), nil
}

func evalSqrt(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	x := asFloat(inputs[0])
	if x < 0 { return kvspace.None{}, fmt.Errorf("sqrt of negative number: %v", x) }
	return kvspace.NewFloat64(math.Sqrt(x)), nil
}

func evalExp(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	return kvspace.NewFloat64(math.Exp(asFloat(inputs[0]))), nil
}

func evalLog(inputs []kvspace.XValue) (kvspace.XValue, error) {
	if err := requireUnary(inputs); err != nil { return kvspace.None{}, err }
	if err := requireNumeric(inputs[0]); err != nil { return kvspace.None{}, err }
	x := asFloat(inputs[0])
	if x <= 0 { return kvspace.None{}, fmt.Errorf("ValueError: log of non-positive number: %v", x) }
	return kvspace.NewFloat64(math.Log(x)), nil
}

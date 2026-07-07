package builtin

import "fmt"

// evalNative 原生算子分发：根据 opcode 路由到对应求值函数。
func evalNative(op string, inputs []nativeValue) (nativeValue, error) {
	switch op {
	case OpAdd:
		return evalBinaryArith(inputs, func(a, b float64) float64 { return a + b })
	case OpSub:
		if len(inputs) == 1 {
			return evalNeg(inputs[0])
		}
		return evalBinaryArith(inputs, func(a, b float64) float64 { return a - b })
	case OpMul:
		return evalBinaryArith(inputs, func(a, b float64) float64 { return a * b })
	case OpDiv:
		return evalDiv(inputs)
	case OpMod:
		return evalMod(inputs)
	case OpEq:
		return evalCmp(inputs, func(a, b float64) bool { return a == b },
			func(a, b string) bool { return a == b })
	case OpNe:
		return evalCmp(inputs, func(a, b float64) bool { return a != b },
			func(a, b string) bool { return a != b })
	case OpLt:
		return evalCmpNum(inputs, func(a, b float64) bool { return a < b })
	case OpGt:
		return evalCmpNum(inputs, func(a, b float64) bool { return a > b })
	case OpLe:
		return evalCmpNum(inputs, func(a, b float64) bool { return a <= b })
	case OpGe:
		return evalCmpNum(inputs, func(a, b float64) bool { return a >= b })
	case OpAnd:
		return evalLogic(inputs, func(a, b bool) bool { return a && b })
	case OpOr:
		return evalLogic(inputs, func(a, b bool) bool { return a || b })
	case OpNot:
		return evalNot(inputs)
	case OpBitAnd:
		return evalBinaryInt(inputs, func(a, b int64) int64 { return a & b })
	case OpBitOr:
		return evalBinaryInt(inputs, func(a, b int64) int64 { return a | b })
	case OpBitXor:
		return evalBinaryInt(inputs, func(a, b int64) int64 { return a ^ b })
	case OpShl:
		return evalBinaryInt(inputs, func(a, b int64) int64 { return a << uint64(b) })
	case OpShr:
		return evalBinaryInt(inputs, func(a, b int64) int64 { return a >> uint64(b) })
	case OpAbs:
		return evalAbs(inputs)
	case OpPow:
		return evalPow(inputs)
	case OpMin:
		return evalMin(inputs)
	case OpMax:
		return evalMax(inputs)
	case OpSqrt:
		return evalSqrt(inputs)
	case OpExp:
		return evalExp(inputs)
	case OpLog:
		return evalLog(inputs)
	case OpNeg:
		return evalUnaryArith(inputs, func(a float64) float64 { return -a })
	case OpSign:
		return evalSign(inputs)
	case OpInt:
		return evalToInt(inputs)
	case OpFloat:
		return evalToFloat(inputs)
	case OpBool:
		return evalToBool(inputs)
	case OpPrint, OpCerr:
		return evalPrint(inputs)
	case OpInput:
		return nativeValue{kind: "string", raw: ""}, nil
	case OpStrSet:
		if len(inputs) > 0 {
			return inputs[0], nil
		}
		return nativeValue{kind: "string", raw: ""}, nil
	default:
		return nativeValue{}, fmt.Errorf("unknown native op: %s", op)
	}
}

func requireBinary(inputs []nativeValue) error {
	if len(inputs) != 2 {
		return fmt.Errorf("binary op requires 2 inputs, got %d", len(inputs))
	}
	return nil
}

func requireUnary(inputs []nativeValue) error {
	if len(inputs) != 1 {
		return fmt.Errorf("unary op requires 1 input, got %d", len(inputs))
	}
	return nil
}

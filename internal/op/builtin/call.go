package builtin

import (
	"context"

	"kvlang/internal/op"
)

var bg = context.Background()

// Op 内建算子接口。
type Op interface {
	Call(f *op.Frame) error
}

// Dispatch 根据 opcode 分发算子。
func Dispatch(opcode string) (Op, bool) {
	o, ok := registry[opcode]
	return o, ok
}

var registry = map[string]Op{
	OpAdd: arith{f: func(a, b float64) float64 { return a + b }, fi: func(a, b int64) int64 { return a + b }, concat: true},
	OpSub: arith{f: func(a, b float64) float64 { return a - b }, fi: func(a, b int64) int64 { return a - b }, unary: true},
	OpMul: arith{f: func(a, b float64) float64 { return a * b }, fi: func(a, b int64) int64 { return a * b }},
	OpDiv: div{},
	OpMod: mod{},

	OpEq: cmp{f: func(a, b float64) bool { return a == b }, i: func(a, b int64) bool { return a == b }, s: func(a, b string) bool { return a == b }},
	OpNe: cmp{f: func(a, b float64) bool { return a != b }, i: func(a, b int64) bool { return a != b }, s: func(a, b string) bool { return a != b }},
	OpLt: cmp{f: func(a, b float64) bool { return a < b }, i: func(a, b int64) bool { return a < b }, s: func(a, b string) bool { return a < b }},
	OpGt: cmp{f: func(a, b float64) bool { return a > b }, i: func(a, b int64) bool { return a > b }, s: func(a, b string) bool { return a > b }},
	OpLe: cmp{f: func(a, b float64) bool { return a <= b }, i: func(a, b int64) bool { return a <= b }, s: func(a, b string) bool { return a <= b }},
	OpGe: cmp{f: func(a, b float64) bool { return a >= b }, i: func(a, b int64) bool { return a >= b }, s: func(a, b string) bool { return a >= b }},

	OpAnd: logic{f: func(a, b bool) bool { return a && b }},
	OpOr:  logic{f: func(a, b bool) bool { return a || b }},
	OpNot: not{},

	OpBitAnd: bit{f: func(a, b int64) int64 { return a & b }},
	OpBitOr:  bit{f: func(a, b int64) int64 { return a | b }},
	OpBitXor: bit{f: func(a, b int64) int64 { return a ^ b }},
	OpShl:    bit{f: func(a, b int64) int64 { return a << uint64(b) }},
	OpShr:    bit{f: func(a, b int64) int64 { return a >> uint64(b) }},

	OpAbs:  mOp{kind: "abs"},
	OpPow:  mOp{kind: "pow"},
	OpMin:  mOp{kind: "min"},
	OpMax:  mOp{kind: "max"},
	OpSqrt: mOp{kind: "sqrt"},
	OpExp:  mOp{kind: "exp"},
	OpLog:  mOp{kind: "log"},
	OpNeg:  mOp{kind: "neg"},
	OpSign: mOp{kind: "sign"},

	OpInt:   cOp{kind: "int"},
	OpFloat: cOp{kind: "float"},
	OpBool:  cOp{kind: "bool"},
	OpInt8:    cOp{kind: "int8"},
	OpInt16:   cOp{kind: "int16"},
	OpInt32:   cOp{kind: "int32"},
	OpInt64:   cOp{kind: "int64"},
	OpUint8:   cOp{kind: "uint8"},
	OpUint16:  cOp{kind: "uint16"},
	OpUint32:  cOp{kind: "uint32"},
	OpUint64:  cOp{kind: "uint64"},
	OpFloat32: cOp{kind: "float32"},
	OpFloat64: cOp{kind: "float64"},

	OpPrint:  ioOp{print: true},
	OpCerr:   ioOp{print: true, cerr: true},
	OpInput:  ioOp{input: true},
	OpStrSet: strOp{},
	OpKVHas:  kvHasOp{},
	OpKVAt:   kvAtOp{},
	OpArray:  arrayOp{},
	OpLen:    lenOp{},
	OpAt:     atOp{},
	OpSet:    arraySetOp{},
	OpHas:    hasOp{},
	OpChar:   strCharOp{},
	OpOrd:    strOrdOp{},
	OpDebugger: debuggerOp{},

	OpStrCmp: strCmpOp{},
	OpStrStr: strStrOp{},
	OpStrLen: strLenOp{},
	OpSlice:  strSliceOp{},
	OpConcat: strConcatOp{},
	OpSort:   sortOp{},
	OpDict:   dictOp{},
}

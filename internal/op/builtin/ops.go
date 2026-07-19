package builtin

// ── 原生算子 opcode 常量 ──

// 算术
const (
	OpAdd = "+"
	OpSub = "-"
	OpMul = "*"
	OpDiv    = "/"
	OpMod = "%"
)

// 比较
const (
	OpEq = "=="
	OpNe = "!="
	OpLt = "<"
	OpGt = ">"
	OpLe = "<="
	OpGe = ">="
)

// 逻辑
const (
	OpAnd = "&&"
	OpOr  = "||"
	OpNot = "!"
)

// 位运算
const (
	OpBitAnd = "&"
	OpBitOr  = "|"
	OpBitXor = "^"
	OpShl    = "<<"
	OpShr    = ">>"
)

// 数学 built-in
const (
	OpAbs  = "abs"
	OpPow  = "pow"
	OpMin  = "min"
	OpMax  = "max"
	OpSqrt = "sqrt"
	OpExp  = "exp"
	OpLog  = "log"
	OpNeg  = "neg"
	OpSign = "sign"
)

// 类型转换 built-in
const (
	OpInt   = "int"
	OpFloat = "float"
	OpBool  = "bool"
	// 全谱数字类型创建/转换算子（fix-021）；int/float 为 int64/float64 别名
	OpInt8    = "int8"
	OpInt16   = "int16"
	OpInt32   = "int32"
	OpInt64   = "int64"
	OpUint8   = "uint8"
	OpUint16  = "uint16"
	OpUint32  = "uint32"
	OpUint64  = "uint64"
	OpFloat32 = "float32"
	OpFloat64 = "float64"
)

// IO built-in
const (
	OpPrint = "print"
	OpCerr  = "cerr"
	OpInput = "input"
)

// 字符串赋值（兼容旧名 str.set 通过 alias 支持）
const (
	OpStrSet = "string.set"
)

// 字符串 built-in
const (
	OpChar   = "char"
	OpOrd    = "ord"
	OpStrCmp = "strcmp"
	OpStrStr = "strstr"
	OpStrLen = "strlen"
	OpSlice  = "slice"
	OpConcat = "concat"
	OpSort   = "sort"
	OpDict   = "dict"
)

// 数组 built-in
const (
	OpArray = "array"
	OpLen   = "len"
	OpAt    = "at"
	OpSet   = "set"
	OpHas   = "has" // kvspace array set
)

// KV 遍历 built-in（for 循环遍历 kvspace 路径子项）
const (
	OpKVHas = "kvhas"
	OpKVAt  = "kvat"
)

// nativeOps 定义 VM 原生求值的算子集合。
var nativeOps = map[string]bool{
	OpAdd: true, OpSub: true, OpMul: true, OpDiv: true, OpMod: true,
	OpEq: true, OpNe: true, OpLt: true, OpGt: true, OpLe: true, OpGe: true,
	OpAnd: true, OpOr: true, OpNot: true,
	OpBitAnd: true, OpBitOr: true, OpBitXor: true, OpShl: true, OpShr: true,
	OpAbs: true, OpPow: true, OpMin: true, OpMax: true, OpSqrt: true, OpExp: true, OpLog: true, OpNeg: true, OpSign: true,
	OpInt: true, OpFloat: true, OpBool: true,
	OpInt8: true, OpInt16: true, OpInt32: true, OpInt64: true,
	OpUint8: true, OpUint16: true, OpUint32: true, OpUint64: true,
	OpFloat32: true, OpFloat64: true,
	OpPrint: true, OpCerr: true, OpInput: true,
	OpStrSet: true,
	OpKVHas: true, OpKVAt: true,
	OpArray: true, OpLen: true, OpAt: true, OpSet: true,
	OpHas: true,
	OpChar: true, OpOrd: true, OpStrCmp: true, OpStrStr: true, OpStrLen: true, OpSlice: true, OpConcat: true,
	OpSort: true,
	OpDict: true,
}

// IsNativeOp 判断是否为 VM 原生求值的符号算子。
func IsNativeOp(opcode string) bool {
	return nativeOps[opcode]
}

// NativeOpList 返回所有 VM 原生算子的有序列表 (仅 opcode)。
func NativeOpList() []string {
	ops := make([]string, 0, len(nativeOps))
	for op := range nativeOps {
		ops = append(ops, op)
	}
	return ops
}

// nativeSigs 定义每个内置算子的 def 签名: 参数读写 + 类型。
// 格式: "def op(reads...) -> (writes...)"
// 类型: num(int/float), int, float, bool, any
var nativeSigs = map[string]string{
	OpAdd: "def +(A:num, B:num) -> (C:num)",
	OpSub: "def -(A:num, B:num) -> (C:num)",
	OpMul: "def *(A:num, B:num) -> (C:num)",
	OpDiv: "def /(A:num, B:num) -> (C:float)",
	OpMod: "def %(A:int, B:int) -> (C:int)",
	OpEq:  "def ==(A:num, B:num) -> (C:bool)",
	OpNe:  "def !=(A:num, B:num) -> (C:bool)",
	OpLt:  "def <(A:num, B:num) -> (C:bool)",
	OpGt:  "def >(A:num, B:num) -> (C:bool)",
	OpLe:  "def <=(A:num, B:num) -> (C:bool)",
	OpGe:  "def >=(A:num, B:num) -> (C:bool)",
	OpAnd: "def &&(A:bool, B:bool) -> (C:bool)",
	OpOr:  "def ||(A:bool, B:bool) -> (C:bool)",
	OpNot: "def !(A:bool) -> (C:bool)",
	OpBitAnd: "def &(A:int, B:int) -> (C:int)",
	OpBitOr:  "def |(A:int, B:int) -> (C:int)",
	OpBitXor: "def ^(A:int, B:int) -> (C:int)",
	OpShl:    "def <<(A:int, B:int) -> (C:int)",
	OpShr:    "def >>(A:int, B:int) -> (C:int)",
	OpAbs:  "def abs(A:num) -> (C:num)",
	OpNeg:  "def neg(A:num) -> (C:num)",
	OpSqrt: "def sqrt(A:num) -> (C:float)",
	OpExp:  "def exp(A:num) -> (C:float)",
	OpLog:  "def log(A:num) -> (C:float)",
	OpPow:  "def pow(A:num, B:num) -> (C:float)",
	OpMin:  "def min(A:num, B:num) -> (C:num)",
	OpMax:  "def max(A:num, B:num) -> (C:num)",
	OpSign: "def sign(A:num) -> (C:int)",
	OpInt:   "def int(A:any) -> (C:int)",
	OpInt8:    "def int8(A:any) -> (C:int8)",
	OpInt16:   "def int16(A:any) -> (C:int16)",
	OpInt32:   "def int32(A:any) -> (C:int32)",
	OpInt64:   "def int64(A:any) -> (C:int64)",
	OpUint8:   "def uint8(A:any) -> (C:uint8)",
	OpUint16:  "def uint16(A:any) -> (C:uint16)",
	OpUint32:  "def uint32(A:any) -> (C:uint32)",
	OpUint64:  "def uint64(A:any) -> (C:uint64)",
	OpFloat32: "def float32(A:any) -> (C:float32)",
	OpFloat64: "def float64(A:any) -> (C:float64)",
	OpFloat: "def float(A:any) -> (C:float)",
	OpBool:  "def bool(A:any) -> (C:bool)",
	OpPrint: "def print(A:any, ...) -> ()",
	OpCerr:  "def cerr(A:any, ...) -> ()",
	OpInput: "def input(prompt:string?) -> (C:string)",
	OpStrSet: "def string.set(A:any) -> (...)",
}

// OpDefs 返回格式化后的算子定义文本列表 (按 opcode 排序)。
// 每个元素为 "def opcode(reads...) -> (writes...)" 格式。
func OpDefs() []string {
	defs := make([]string, 0, len(nativeSigs))
	for op := range nativeOps {
		if sig, ok := nativeSigs[op]; ok {
			defs = append(defs, sig)
		} else {
			defs = append(defs, "def "+op+"() -> ()")
		}
	}
	return defs
}

// IsUnaryNativeOp 判断是否为单目原生算子。
func IsUnaryNativeOp(opcode string) bool {
	switch opcode {
	case OpNot, OpSub, OpAbs, OpSqrt, OpExp, OpLog, OpNeg, OpSign,
		OpInt, OpFloat, OpBool:
		return true
	}
	return false
}

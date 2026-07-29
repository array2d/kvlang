package builtin

import (
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
	"strings"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/op"
	"kvlang/vthread"
)

// arrayOp: [e1, e2, ...] → typed array XValue。
// 目标类型由写槽的类型标注决定（如 arr:int32 = [1,2,3] → int32 同构数组）。
// 无类型标注时回退为异构 TLV 数组。
type arrayOp struct{}
func (arrayOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(f.Inst.Writes) == 0 {
		vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
		return nil
	}
	frameRoot := keytree.FrameRoot(f.PC)
	outKey := resolveWriteKey(f.KV, frameRoot, f.Inst.Writes[0])
	// 从指令写槽 kind 取目标类型
	targetKind := ""
	if len(f.Inst.WriteKinds) > 0 && f.Inst.WriteKinds[0] != "rwir" {
		targetKind = f.Inst.WriteKinds[0]
	}
	if targetKind != "" {
		arr := packTypedArray(targetKind, inputs)
		f.KV.Set([]kvspace.KVPair{{outKey, arr}})
	} else {
		arr := kvspace.Array(inputs)
		f.KV.Set([]kvspace.KVPair{{outKey, arr}})
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// packTypedArray 将元素按 kind 打包为同构定长数组。
// 铁律：所有元素 kind 必须匹配目标 kind。
func packTypedArray(kind string, elems []kvspace.XValue) kvspace.XValue {
	sz := kindSize(kind)
	if sz <= 0 { return kvspace.Array(elems) }
	raw := make([]byte, int32(len(elems))*sz)
	for i, e := range elems {
		if e.Kind() != kind {
			panic("packTypedArray: element " + itoa(i) + " kind mismatch: expected " + kind + ", got " + e.Kind())
		}
		copy(raw[i*int(sz):], kindBytes(kind, e))
	}
	return kvspace.RawN(kind, raw, int32(len(elems)))
}

func itoa(i int) string { return strconv.Itoa(i) }

func kindSize(kind string) int32 {
	switch kind {
	case "bool", "int8", "uint8": return 1
	case "int16", "uint16": return 2
	case "int32", "uint32", "float32": return 4
	case "int64", "uint64", "float64": return 8
	default: return 0
	}
}

func kindBytes(kind string, v kvspace.XValue) []byte {
	switch kind {
	case "bool":
		if AsBool(v) { return []byte{1} }; return []byte{0}
	case "int8": return []byte{byte(int8(asInt(v)))}
	case "uint8": return []byte{uint8(asInt(v))}
	case "int16": b := make([]byte, 2); binary.LittleEndian.PutUint16(b, uint16(int16(asInt(v)))); return b
	case "uint16": b := make([]byte, 2); binary.LittleEndian.PutUint16(b, uint16(asInt(v))); return b
	case "int32": b := make([]byte, 4); binary.LittleEndian.PutUint32(b, uint32(int32(asInt(v)))); return b
	case "uint32": b := make([]byte, 4); binary.LittleEndian.PutUint32(b, uint32(asInt(v))); return b
	case "float32": b := make([]byte, 4); binary.LittleEndian.PutUint32(b, math.Float32bits(float32(asFloat(v)))); return b
	case "int64": b := make([]byte, 8); binary.LittleEndian.PutUint64(b, uint64(asInt(v))); return b
	case "uint64": b := make([]byte, 8); binary.LittleEndian.PutUint64(b, uint64(asInt(v))); return b
	case "float64": b := make([]byte, 8); binary.LittleEndian.PutUint64(b, math.Float64bits(asFloat(v))); return b
	default: return v.RawBytes()
	}
}

// lenOp: len(array) → int。异构数组用 Len()，同构数组用 ArrayLen()。
type lenOp struct{}
func (lenOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 {
		n = inputs[0].Len()
		if n == 0 { n = int(inputs[0].ArrayLen()) }
	}
	return writeResult(f, kvspace.Int64(int64(n)))
}

// atOp: at(array, index) → element
type atOp struct{}
func (atOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: at requires array and index")
		return fmt.Errorf("TypeError: at requires array and index")
	}
	// string base 分流（fix-025）：以 "/" 开头 = 路径指针（键族 deref）；否则 = 字符序列。
	// s[i] 读返单字符字符串（动态阵营，与 char 一致）；越界/非整型索引返 ""（缺席语义）。
	if inputs[0].Kind() == "string" && !strings.HasPrefix(inputs[0].Str(), "/") {
		s := inputs[0].Str()
		if !isIntKind(inputs[1].Kind()) {
			return writeResult(f, kvspace.Str(""))
		}
		idx := int(inputs[1].Int64())
		if idx < 0 || idx >= len(s) {
			return writeResult(f, kvspace.Str(""))
		}
		return writeResult(f, kvspace.Str(s[idx:idx+1]))
	}
	// 路径访问：at(/path, key) or at(ptr, "field") or h.*key
	// 排除 typed array（int32/float64/…）：用字符串 key 对同构数组做路径访问是错误。
	if (inputs[0].Kind() == "dict" || inputs[0].Kind() == "string" || inputs[1].Kind() == "string" || len(f.Inst.Reads) > 0 && (f.Inst.Reads[0][0] == '/' || f.Inst.Reads[0][0] == '"' && len(f.Inst.Reads[0]) > 1 && f.Inst.Reads[0][1] == '/')) && kindSize(inputs[0].Kind()) <= 0 {
		fp := keytree.FrameRoot(f.PC)
		funcFrame := funcFrameRoot(f.KV, fp)
		base := resolveReadValue(f.KV, fp, f.Inst.Reads[0]).Str()
		if base == "" {
			raw := f.Inst.Reads[0]
			if len(raw) > 1 && raw[0] == '"' { raw = raw[1:] }
			base = resolveKVPath(funcFrame, raw)
		}
		path := keytree.Member(base, kvKey(inputs[1]))
		v := kvspace.GetOne(f.KV, path); return writeResult(f, v)
	}
	// 拒绝 string 索引 typed array（五语言中仅 JS 允许，C/Python/Rust/Go 均编译/运行时拒绝）
	if kindSize(inputs[0].Kind()) > 0 && inputs[1].Kind() == "string" {
		msg := "IndexError: at: index must be integer for typed array"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	if inputs[0].IsNone() {
		msg := "IndexError: at: base " + f.Inst.Reads[0] + " is None; help: declare a key-family first (e.g. `" + f.Inst.Reads[0] + " = {}`) or pass a path string"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	idx := int(inputs[1].Int64())
	elem := inputs[0].Index(idx)
	if elem.IsNone() {
		elem = typedIndex(inputs[0], idx)
	}
	if elem.IsNone() {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: index %d out of bounds; help: the array or string has no item at this position", idx))
		return fmt.Errorf("IndexError: at: index out of bounds")
	}
	return writeResult(f, elem)
}

// typedIndex 用同构数组的 arraylength + 定长偏移读取元素。
func typedIndex(v kvspace.XValue, idx int) kvspace.XValue {
	n := int(v.ArrayLen())
	if idx < 0 || idx >= n { return kvspace.XValue{} }
	k := v.Kind()
	sz := kindSize(k)
	if sz <= 0 { return kvspace.XValue{} }
	off := idx * int(sz)
	if off+int(sz) > len(v.RawBytes()) { return kvspace.XValue{} }
	raw := make([]byte, sz)
	copy(raw, v.RawBytes()[off:off+int(sz)])
	return kvspace.RawN(k, raw, 1)
}

// arraySetOp: set(array, index, value) → modified array
type arraySetOp struct{}
func (arraySetOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 3 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: set requires array, index, value")
		return fmt.Errorf("TypeError: set requires array, index, value")
	}
	arr := inputs[0]
	// string base 分流（fix-025）：非 "/" 开头 = 字符序列，s[i] 写 = 单字符替换后整串回写
	// （C 直觉 + kvlang 值语义；五语言中仅 C 可变，Python/Go/Rust/JS 字符串不可变，
	//   kvlang 以"写回新串"呈现 C 直觉、保持值语义）。越界报错（C 为 UB，此处显式）。
	if arr.Kind() == "string" && !strings.HasPrefix(arr.Str(), "/") {
		sv := arr.Str()
		idx := int(inputs[1].Int64())
		ch := inputs[2].Str()
		if idx < 0 || idx >= len(sv) {
			msg := fmt.Sprintf("IndexError: set: string index %d out of bounds (len=%d); help: try adjusting the index or check string.len first", idx, len(sv))
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
			return fmt.Errorf("%s", msg)
		}
		if len(ch) == 0 {
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: set: replacement char is empty")
			return fmt.Errorf("TypeError: set: replacement char is empty")
		}
		result := sv[:idx] + ch[:1] + sv[idx+1:]
		return writeResult(f, kvspace.Str(result))
	}
	// 路径写入：set(/path, key, val) or set(ptr, "field", val)
	// 排除 typed array（int32/float64/…）：用字符串 key 对同构数组做路径访问是错误。
	if (inputs[0].Kind() == "dict" || inputs[0].Kind() == "string" || inputs[1].Kind() == "string" || len(f.Inst.Reads) > 0 && (f.Inst.Reads[0][0] == '/' || f.Inst.Reads[0][0] == '"' && len(f.Inst.Reads[0]) > 1 && f.Inst.Reads[0][1] == '/')) && kindSize(inputs[0].Kind()) <= 0 {
		fp := keytree.FrameRoot(f.PC)
		funcFrame := funcFrameRoot(f.KV, fp)
		base := resolveReadValue(f.KV, fp, f.Inst.Reads[0]).Str()
		if base == "" {
			raw := f.Inst.Reads[0]
			if len(raw) > 1 && raw[0] == '"' { raw = raw[1:] }
			base = resolveKVPath(funcFrame, raw)
		}
		path := keytree.Member(base, kvKey(inputs[1]))
		f.KV.Set([]kvspace.KVPair{{path, inputs[2]}})
		if len(f.Inst.Writes) > 0 && !inputs[0].IsNone() {
			// 写入 base 本身（值不变），满足 -> base 返回槽
			outKey := resolveWriteKey(f.KV, fp, f.Inst.Writes[0])
			f.KV.Set([]kvspace.KVPair{{outKey, inputs[0]}})
		}
		vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
		return nil
	}
	// 拒绝 string 索引 typed array（五语言中仅 JS 允许，C/Python/Rust/Go 均编译/运行时拒绝）
	if kindSize(arr.Kind()) > 0 && inputs[1].Kind() == "string" {
		msg := "IndexError: set: index must be integer for typed array"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	if arr.IsNone() {
		msg := "IndexError: set: base " + f.Inst.Reads[0] + " is None; help: declare a key-family first (e.g. `" + f.Inst.Reads[0] + " = {}`) or pass a path string"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	// 数组长度判定
	n := int(arr.ArrayLen())
	if n == 0 { n = arr.Len() }
	idx := int(inputs[1].Int64())
	if idx < 0 || idx >= n {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: set: index %d out of bounds (len=%d)", idx, n))
		return fmt.Errorf("set: index out of bounds")
	}
	val := inputs[2]
	var result kvspace.XValue
	k := arr.Kind()
	if kindSize(k) > 0 {
		// 定长类型：原地替换
		raw := make([]byte, len(arr.RawBytes()))
		copy(raw, arr.RawBytes())
		sz := int(kindSize(k))
		b := kindBytes(k, val)
		copy(raw[idx*sz:], b)
		result = kvspace.RawN(k, raw, int32(n))
	} else {
		// 变长类型（string 等）：重建 TLV
		encoded := make([][]byte, n)
		total := 4
		for i := 0; i < n; i++ {
			elem := arr.Index(i)
			if i == idx { elem = val }
			encoded[i] = kvspace.EncodeXValue(elem)
			total += len(encoded[i])
		}
		raw := make([]byte, total)
		binary.LittleEndian.PutUint32(raw[:4], uint32(n))
		off := 4
		for _, enc := range encoded {
			copy(raw[off:], enc)
			off += len(enc)
		}
		result = kvspace.RawN(k, raw, int32(n))
	}
	if len(f.Inst.Writes) > 0 {
		outKey := resolveWriteKey(f.KV, keytree.FrameRoot(f.PC), f.Inst.Writes[0])
		f.KV.Set([]kvspace.KVPair{{outKey, result}})
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

// sortOp: bubble sort (in-place, returns sorted copy).
// 铁律：元素必须全数字或全字符串；混合类型/不可比类型 → TypeError。
type sortOp struct{}
func (sortOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 { return writeResult(f, kvspace.XValue{}) }
	arr := inputs[0]
	n := arr.Len()
	if n <= 1 { return writeResult(f, arr) }
	elems := make([]kvspace.XValue, n)
	allNum, allStr := true, true
	for i := 0; i < n; i++ {
		elems[i] = arr.Index(i)
		if !isNumeric(elems[i]) { allNum = false }
		if elems[i].Kind() != "string" { allStr = false }
	}
	if !allNum && !allStr {
		msg := "TypeError: sort requires all elements to be numeric or all string"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	// bubble sort
	for i := 0; i < n-1; i++ {
		for j := 0; j < n-i-1; j++ {
			var swap bool
			if allNum {
				swap = asFloat(elems[j]) > asFloat(elems[j+1])
			} else {
				swap = elems[j].Str() > elems[j+1].Str()
			}
			if swap {
				elems[j], elems[j+1] = elems[j+1], elems[j]
			}
		}
	}
	result := kvspace.Array(elems)
	return writeResult(f, result)
}

// hasOp: has(path, key) → bool — kvspace 路径存在性检查
type hasOp struct{}
func (hasOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.Bool(false)) }
	fp := keytree.FrameRoot(f.PC)
	funcFrame := funcFrameRoot(f.KV, fp)
	base := resolveReadValue(f.KV, fp, f.Inst.Reads[0]).Str()
	if base == "" { base = resolveKVPath(funcFrame, f.Inst.Reads[0]) }
	key := kvKey(inputs[1])
	v := kvspace.GetOne(f.KV, keytree.Member(base, key))
	return writeResult(f, kvspace.Bool(!v.IsNone()))
}

func kvKey(v kvspace.XValue) string {
	if v.Kind() == "string" { return v.Str() }
	if isIntKind(v.Kind()) { return strconv.FormatInt(v.Int64(), 10) }
	panic("kvKey: expected string or int kind, got " + v.Kind())
}

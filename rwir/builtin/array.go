package builtin

import (
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
	"strings"

	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	Register("array", "", arrayOp{})
	Register("len",   "", lenOp{})
	Register("at",    "", atOp{})
	Register("set",   "", arraySetOp{})
	Register("has",   "", hasOp{})
	Register("sort",  "", sortOp{})
	Register("scatter", "", scatterOp{})
	Register("compact", "", compactOp{})
}

// arrayOp: [e1, e2, ...] → typed array XValue。
// 目标类型由写槽的类型标注决定（如 arr:int32 = [1,2,3] → int32 同构数组）。
// 无类型标注时回退为异构 TLV 数组。
type arrayOp struct{}
func (arrayOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(f.Inst.Writes) == 0 {
		vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
		return nil
	}
	frameRoot := keytree.FrameRoot(f.PC)
	outKey := writeSlotKey(f.KV, frameRoot, f.Inst.Writes[0].Name)
	// 所有数组必须同构。空数组回退 int64。
	if len(inputs) == 0 {
		vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
		return nil
	}
	targetKind := inputs[0].Kind()
	if kvspace.ElemSize(targetKind) <= 0 {
		panic("array: unsupported element kind " + targetKind)
	}
	for _, e := range inputs[1:] {
		if e.Kind() != targetKind {
			panic("array: mixed kinds " + targetKind + " and " + e.Kind())
		}
	}
	arr := packTypedArray(targetKind, inputs)
	f.KV.Set([]kvspace.KVPair{{Key: outKey, Val: arr}})
	vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
	return nil
}

// packTypedArray 将元素按 kind 打包为同构定长数组。
// 铁律：所有元素 kind 必须匹配目标 kind。
func packTypedArray(kind string, elems []kvspace.XValue) kvspace.XValue {
	sz := kvspace.ElemSize(kind)
	if sz <= 0 { panic("packTypedArray: kind " + kind + " has no fixed element size") }
	raw := make([]byte, int32(len(elems))*sz)
	for i, e := range elems {
		if e.Kind() != kind {
			panic("packTypedArray: element " + itoa(i) + " kind mismatch: expected " + kind + ", got " + e.Kind())
		}
		copy(raw[i*int(sz):], kindBytes(kind, e))
	}
	return rawDecodeN(kind, raw, int32(len(elems)))
}

func itoa(i int) string { return strconv.Itoa(i) }

func kindBytes(kind string, v kvspace.XValue) []byte {
	switch kind {
	case "bool":
		if AsBool(v) { return []byte{1} }; return []byte{0}
	case "int8": return []byte{byte(int8(asInt64(v)))}
	case "uint8": return []byte{uint8(asInt64(v))}
	case "int16": b := make([]byte, 2); binary.LittleEndian.PutUint16(b, uint16(int16(asInt64(v)))); return b
	case "uint16": b := make([]byte, 2); binary.LittleEndian.PutUint16(b, uint16(asInt64(v))); return b
	case "int32": b := make([]byte, 4); binary.LittleEndian.PutUint32(b, uint32(int32(asInt64(v)))); return b
	case "uint32": b := make([]byte, 4); binary.LittleEndian.PutUint32(b, uint32(asInt64(v))); return b
	case "float32": b := make([]byte, 4); binary.LittleEndian.PutUint32(b, math.Float32bits(float32(asFloat(v)))); return b
	case "int64": b := make([]byte, 8); binary.LittleEndian.PutUint64(b, uint64(asInt64(v))); return b
	case "uint64": b := make([]byte, 8); binary.LittleEndian.PutUint64(b, uint64(asInt64(v))); return b
	case "float64": b := make([]byte, 8); binary.LittleEndian.PutUint64(b, math.Float64bits(asFloat(v))); return b
	default: return rawBytesOf(v)
	}
}

// lenOp: len(array) → int。异构数组用 Len()，同构数组用 ArrayLen()。
type lenOp struct{}
func (lenOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	n := 0
	if len(inputs) > 0 {
		n = int(inputs[0].ArrayLen())
	}
	return writeResult(f, kvspace.NewInt64(int64(n)))
}

// atOp: at(array, index) → element
type atOp struct{}
func (atOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: at requires array and index")
		return fmt.Errorf("TypeError: at requires array and index")
	}
	// string base 分流（fix-025）：以 "/" 开头 = 路径指针（键族 deref）；否则 = 字符序列。
	// s[i] 读返单字符字符串（动态阵营，与 char 一致）；越界/非整型索引返 ""（缺席语义）。
	if inputs[0].Kind() == "stringbyte" && !strings.HasPrefix(inputs[0].ValueString(), "/") {
		s := inputs[0].ValueString()
		if !isIntKind(inputs[1].Kind()) {
			return writeResult(f, kvspace.NewStringByte([]byte("")...))
		}
		idx := int(asInt64(inputs[1]))
		if idx < 0 || idx >= len(s) {
			return writeResult(f, kvspace.NewStringByte([]byte("")...))
		}
		return writeResult(f, kvspace.NewStringByte([]byte(s[idx:idx+1])...))
	}
	// 路径访问：at(/path, key) or at(ptr, "field") or h.*key
	// 排除 typed array（int32/float64/…）：用字符串 key 对同构数组做路径访问是错误。
	if (inputs[0].Kind() == "dict" || inputs[0].Kind() == "stringbyte" || inputs[1].Kind() == "stringbyte" || len(f.Inst.Reads) > 0 && (f.Inst.Reads[0].Name[0] == '/' || f.Inst.Reads[0].Name[0] == '"' && len(f.Inst.Reads[0].Name) > 1 && f.Inst.Reads[0].Name[1] == '/'))  {
		fp := keytree.FrameRoot(f.PC)
		funcFrame := funcFrameRoot(f.KV, fp)
		base := resolveBasePath(f.KV, fp, funcFrame, f.Inst.Reads[0])
		path := keytree.Member(base, kvKey(inputs[1]))
		v := kvspace.GetOne(f.KV, path); return writeResult(f, v)
	}
	// 拒绝 string 索引 typed array（五语言中仅 JS 允许，C/Python/Rust/Go 均编译/运行时拒绝）
	if kvspace.ElemSize(inputs[0].Kind()) > 0 && inputs[1].Kind() == "stringbyte" {
		msg := "IndexError: at: index must be integer for typed array"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	if kvspace.IsNone(inputs[0]) {
		msg := "IndexError: at: base " + f.Inst.Reads[0].Name + " is None; help: declare a key-family first (e.g. `" + f.Inst.Reads[0].Name + " = {}`) or pass a path string"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	idx := int(asInt64(inputs[1]))
	// 同构数组：整读后内存内下标
	if kvspace.ElemSize(inputs[0].Kind()) > 0 {
		elem := typedIndex(inputs[0], idx)
		return writeResult(f, elem)
	}
	elem := typedIndex(inputs[0], idx)
	if kvspace.IsNone(elem) {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("IndexError: at: index %d out of bounds", idx))
		return fmt.Errorf("IndexError: at: index out of bounds")
	}
	return writeResult(f, elem)
}

















// setElem 整存整取：读整个数组，内存内改 idx 元素，写回整个数组。
func setElem(f *rwir.Frame, slotName string, idx int32, val kvspace.XValue) {
	fp := keytree.FrameRoot(f.PC)
	rwRoot := funcFrameRoot(f.KV, fp)
	key := keytree.Stack(rwRoot) + slotName
	if ptrVal := kvspace.GetOne(f.KV, key); kvspace.IsPtr(ptrVal) {
		argAddr := kvspace.GetOne(f.KV, keytree.Stack(rwRoot)+kvspace.PtrTarget(ptrVal))
		if !kvspace.IsNone(argAddr) {
			key = argAddr.ValueString()
		}
	}
	arr := kvspace.GetOne(f.KV, key)
	if kvspace.IsNone(arr) {
		return
	}
	f.KV.Set([]kvspace.KVPair{{Key: key, Val: setArrayElem(arr, int(idx), val)}})
}

// setArrayElem 返回将 arr 第 idx 元素替换为 val 后的副本（同构定长数组，val 按 kind 窄化）。
func setArrayElem(arr kvspace.XValue, idx int, val kvspace.XValue) kvspace.XValue {
	kind := arr.Kind()
	es := int(kvspace.ElemSize(kind))
	body := kvspace.BodyBytes(arr)
	newBody := make([]byte, len(body))
	copy(newBody, body)
	copy(newBody[idx*es:], kindBytes(kind, val))
	return rawDecodeN(kind, newBody, int32(len(newBody)/es))
}

// typedIndex 用同构数组的 arraylength + 定长偏移读取元素（回退路径）。
func typedIndex(v kvspace.XValue, idx int) kvspace.XValue {
	n := int(v.ArrayLen())
	if idx < 0 || idx >= n { return kvspace.None{} }
	k := v.Kind()
	sz := kvspace.ElemSize(k)
	if sz <= 0 { return kvspace.None{} }
	off := idx * int(sz)
	if off+int(sz) > len(rawBytesOf(v)) { return kvspace.None{} }
	raw := make([]byte, sz)
	copy(raw, rawBytesOf(v)[off:off+int(sz)])
	return rawDecodeN(k, raw, 1)
}

// arraySetOp: set(array, index, value) → modified array
type arraySetOp struct{}
func (arraySetOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 3 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: set requires array, index, value")
		return fmt.Errorf("TypeError: set requires array, index, value")
	}
	arr := inputs[0]
	// string base 分流（fix-025）：非 "/" 开头 = 字符序列，s[i] 写 = 单字符替换后整串回写
	// （C 直觉 + kvlang 值语义；五语言中仅 C 可变，Python/Go/Rust/JS 字符串不可变，
	//   kvlang 以"写回新串"呈现 C 直觉、保持值语义）。越界报错（C 为 UB，此处显式）。
	if arr.Kind() == "stringbyte" && !strings.HasPrefix(arr.ValueString(), "/") {
		sv := arr.ValueString()
		idx := int(asInt64(inputs[1]))
		ch := inputs[2].ValueString()
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
		return writeResult(f, kvspace.NewStringByte([]byte(result)...))
	}
	// 路径写入：set(/path, key, val) or set(ptr, "field", val)
	// 排除 typed array（int32/float64/…）：用字符串 key 对同构数组做路径访问是错误。
	if (inputs[0].Kind() == "dict" || inputs[0].Kind() == "stringbyte" || inputs[1].Kind() == "stringbyte" || len(f.Inst.Reads) > 0 && (f.Inst.Reads[0].Name[0] == '/' || f.Inst.Reads[0].Name[0] == '"' && len(f.Inst.Reads[0].Name) > 1 && f.Inst.Reads[0].Name[1] == '/'))  {
		fp := keytree.FrameRoot(f.PC)
		funcFrame := funcFrameRoot(f.KV, fp)
		base := resolveBasePath(f.KV, fp, funcFrame, f.Inst.Reads[0])
		path := keytree.Member(base, kvKey(inputs[1]))
		f.KV.Set([]kvspace.KVPair{{Key: path, Val: inputs[2]}})
		if len(f.Inst.Writes) > 0 && !kvspace.IsNone(inputs[0]) {
			// 写入 base 本身（值不变），满足 -> base 返回槽
			outKey := writeSlotKey(f.KV, fp, f.Inst.Writes[0].Name)
			f.KV.Set([]kvspace.KVPair{{Key: outKey, Val: inputs[0]}})
		}
		vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
		return nil
	}
	// 拒绝 string 索引 typed array（五语言中仅 JS 允许，C/Python/Rust/Go 均编译/运行时拒绝）
	if kvspace.ElemSize(arr.Kind()) > 0 && inputs[1].Kind() == "stringbyte" {
		msg := "IndexError: set: index must be integer for typed array"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	if kvspace.IsNone(arr) {
		msg := "IndexError: set: base " + f.Inst.Reads[0].Name + " is None; help: declare a key-family first (e.g. `" + f.Inst.Reads[0].Name + " = {}`) or pass a path string"
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
		return fmt.Errorf("%s", msg)
	}
	idx := int(asInt64(inputs[1]))
	// 同构数组：整存整取写入元素
	if kvspace.ElemSize(arr.Kind()) > 0 {
		setElem(f, f.Inst.Reads[0].Name, int32(idx), inputs[2])
		vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
		return nil
	}
	msg := "IndexError: set: unsupported array kind " + arr.Kind() + " (string array does not exist; use Char index via s[i])"
	vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
	return fmt.Errorf("%s", msg)
}

// sortOp: bubble sort (in-place, returns sorted copy).
// 铁律：元素必须全数字或全字符串；混合类型/不可比类型 → TypeError。
type sortOp struct{}
func (sortOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 { return writeResult(f, kvspace.None{}) }
	arr := inputs[0]
	n := int(arr.ArrayLen())
	if n <= 1 { return writeResult(f, arr) }
	elems := make([]kvspace.XValue, n)
	for i := 0; i < n; i++ {
		elems[i] = typedIndex(arr, i)
		if kvspace.IsNone(elems[i]) {
			msg := "TypeError: sort: element at index " + itoa(i) + " is None"
			vthread.SetError(bg, f.KV, f.Vtid, f.PC, msg)
			return fmt.Errorf("%s", msg)
		}
	}
	// bubble sort
	for i := 0; i < n-1; i++ {
		for j := 0; j < n-i-1; j++ {
			swap := asFloat(elems[j]) > asFloat(elems[j+1])
			if swap {
				elems[j], elems[j+1] = elems[j+1], elems[j]
			}
		}
	}
	result := packTypedArray(arr.Kind(), elems)
	return writeResult(f, result)
}

// hasOp: has(path, key) → bool — kvspace 路径存在性检查
type hasOp struct{}
func (hasOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 { return writeResult(f, kvspace.NewBool(false)) }
	fp := keytree.FrameRoot(f.PC)
	funcFrame := funcFrameRoot(f.KV, fp)
	base := resolveBasePath(f.KV, fp, funcFrame, f.Inst.Reads[0])
	key := kvKey(inputs[1])
	v := kvspace.GetOne(f.KV, keytree.Member(base, key))
	return writeResult(f, kvspace.NewBool(!kvspace.IsNone(v)))
}

// scatterOp: 解压 []type -> <>type。紧凑数组 arr（一个 packed body）拆成 arr<0>..arr<N-1> 标量 key，删除 arr。
type scatterOp struct{}
func (scatterOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) == 0 || kvspace.IsNone(inputs[0]) || kvspace.ElemSize(inputs[0].Kind()) <= 0 {
		return writeResult(f, kvspace.None{})
	}
	arr := inputs[0]
	fp := keytree.FrameRoot(f.PC)
	base := resolveWriteSlot(f.KV, fp, f.Inst.Reads[0].Name)
	n := int(arr.ArrayLen())
	pairs := make([]kvspace.KVPair, 0, n)
	for i := 0; i < n; i++ {
		pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s<%d>", base, i), Val: typedIndex(arr, i)})
	}
	if len(pairs) > 0 {
		f.KV.Set(pairs)
		f.KV.Del(base)
	}
	return writeResult(f, arr)
}

// compactOp: 压缩 <>type -> []type。arr<0>..arr<N-1> 标量 key 打包成紧凑数组 arr（一个 packed body），删除 item key。
type compactOp struct{}
func (compactOp) Call(f *rwir.Frame) error {
	fp := keytree.FrameRoot(f.PC)
	base := resolveWriteSlot(f.KV, fp, f.Inst.Reads[0].Name)
	var elems []kvspace.XValue
	for i := 0; ; i++ {
		v := kvspace.GetOne(f.KV, fmt.Sprintf("%s<%d>", base, i))
		if kvspace.IsNone(v) {
			break
		}
		elems = append(elems, v)
	}
	if len(elems) == 0 {
		return writeResult(f, kvspace.None{})
	}
	arr := packTypedArray(elems[0].Kind(), elems)
	f.KV.Set([]kvspace.KVPair{{Key: base, Val: arr}})
	for i := range elems {
		f.KV.Del(fmt.Sprintf("%s<%d>", base, i))
	}
	return writeResult(f, arr)
}

func kvKey(v kvspace.XValue) string {
	if v.Kind() == "stringbyte" { return v.ValueString() }
	if isIntKind(v.Kind()) { return strconv.FormatInt(asInt64(v), 10) }
	panic("kvKey: expected string or int kind, got " + v.Kind())
}

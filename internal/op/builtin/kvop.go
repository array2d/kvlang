package builtin

import (
	"fmt"
	"strconv"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// kvHasOp 检查 kvspace 路径是否存在。
// kv.has(prefix, idx) -> bool
//   prefix — 路径字符串（相对 ./x 或绝对 /abs），不做 KV 值查找
//   idx    — 整数索引，从 read slot 解析
type kvHasOp struct{}
func (o kvHasOp) Call(f *op.Frame) error {
	if len(f.Inst.Reads) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "kv.has requires 2 args")
		return fmt.Errorf("kv.has requires 2 args")
	}
	prefix := resolveKVPath(keytree.FrameRoot(f.PC), f.Inst.Reads[0])
	idxVal := resolveReadValue(f.KV, keytree.FrameRoot(f.PC), f.Inst.Reads[1])
	idx := int(idxVal.Int())
	key := prefix + "/" + strconv.Itoa(idx)
	v, _ := f.KV.Get(key)
	return writeResult(f, kvspace.Bool(!v.IsNil()))
}

// kvAtOp 读取 kvspace 路径的值。
// kv.at(prefix, idx) -> value
//   prefix — 路径字符串（相对 ./x 或绝对 /abs），不做 KV 值查找
//   idx    — 整数索引
type kvAtOp struct{}
func (o kvAtOp) Call(f *op.Frame) error {
	if len(f.Inst.Reads) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "kv.at requires 2 args")
		return fmt.Errorf("kv.at requires 2 args")
	}
	prefix := resolveKVPath(keytree.FrameRoot(f.PC), f.Inst.Reads[0])
	idxVal := resolveReadValue(f.KV, keytree.FrameRoot(f.PC), f.Inst.Reads[1])
	idx := int(idxVal.Int())
	key := prefix + "/" + strconv.Itoa(idx)
	v, _ := f.KV.Get(key)
	if v.IsNil() {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("kv.at: key not found: %s", key))
		return fmt.Errorf("kv.at: key not found: %s", key)
	}
	return writeResult(f, v)
}

// resolveKVPath 将槽参数解析为绝对 KV 路径（不做值查找）。
// ./x → framePath/x,  /abs → /abs,  x → framePath/x
func resolveKVPath(framePath, param string) string {
	if isRelative(param) {
		return framePath + "/" + param[2:]
	}
	if isAbsolute(param) {
		return param
	}
	return framePath + "/" + param
}

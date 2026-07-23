package builtin

import (
	"fmt"
	"strconv"

	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// kvHasOp: kv.has(prefix, idx) -> bool
//   若 prefix 为变量名（非路径），先从帧中读其值（路径字符串），再检查路径是否存在。
type kvHasOp struct{}
func (o kvHasOp) Call(f *op.Frame) error {
	if len(f.Inst.Reads) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: kvhas requires 2 args")
		return fmt.Errorf("TypeError: kvhas requires 2 args")
	}
	fp := keytree.FrameRoot(f.PC)
	prefix := resolveReadValue(f.KV, fp, f.Inst.Reads[0]).Str()
	if prefix == "" {
		// 退化为路径字面量解析
		prefix = resolveKVPath(fp, f.Inst.Reads[0])
	}
	idxVal := resolveReadValue(f.KV, fp, f.Inst.Reads[1])
	key := keytree.Member(prefix, strconv.Itoa(int(idxVal.Int64())))
	v := kvspace.GetOne(f.KV, key)
	return writeResult(f, kvspace.Bool(!v.IsNil()))
}

// kvAtOp: kv.at(prefix, idx) -> value
//   若 prefix 为变量名，先从帧中读其值（路径字符串），再用该路径访问。
//   idx 支持整数索引和字符串索引（路径段名）。
type kvAtOp struct{}
func (o kvAtOp) Call(f *op.Frame) error {
	if len(f.Inst.Reads) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: kvat requires 2 args")
		return fmt.Errorf("TypeError: kvat requires 2 args")
	}
	fp := keytree.FrameRoot(f.PC)
	prefix := resolveReadValue(f.KV, fp, f.Inst.Reads[0]).Str()
	if prefix == "" {
		prefix = resolveKVPath(fp, f.Inst.Reads[0])
	}
	idxVal := resolveReadValue(f.KV, fp, f.Inst.Reads[1])
	var key string
	if isIntKind(idxVal.Kind()) || isFloatKind(idxVal.Kind()) {
		key = keytree.Member(prefix, strconv.Itoa(int(idxVal.Int64())))
	} else {
		key = keytree.Member(prefix, idxVal.Str())
	}
	v := kvspace.GetOne(f.KV, key)
	if v.IsNil() {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC,
			fmt.Sprintf("KeyError: kvat: key not found: %s; help: verify the key exists in the path or key-family", key))
		return fmt.Errorf("kvat: key not found: %s", key)
	}
	return writeResult(f, v)
}

// resolveKVPath 将槽参数解析为绝对 KV 路径（不做值查找）。
func resolveKVPath(framePath, param string) string {
	if isAbsolute(param) { return param }
	return framePath + "/" + param
}

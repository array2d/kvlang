package builtin

import (
	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
)

func init() {
	Register("dict", "", dictOp{})
}

// dictOp: dict(k1, v1, k2, v2, ...) -> base —— dict 字面量 { k1=v1; k2=v2 } 的运行时。
// base 键写入 kind="dict" 类型标记，成员写入平坦键族 base.k（keytree.Member）。
// 值为 null（如 null 裸名解析结果）时跳过写入——kvspace 中缺席即 null。
type dictOp struct{}
func (dictOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	fp := keytree.FrameRoot(f.PC)
	var pairs []kvspace.KVPair
	for _, w := range f.Inst.Writes {
		outKey := writeSlotKey(f.KV, fp, w.Name)
		pairs = append(pairs, kvspace.KVPair{outKey, kvspace.Dict{}})
		for i := 0; i+1 < len(inputs); i += 2 {
			if kvspace.IsNone(inputs[i+1]) { continue }
			pairs = append(pairs, kvspace.KVPair{keytree.Member(outKey, inputs[i].ValueString()), inputs[i+1]})
		}
	}
	if len(pairs) > 0 { f.KV.Set(pairs) }
	vthread.Set(bg, f.KV, f.Vtid, rwir.NextPC(f.PC), "running")
	return nil
}

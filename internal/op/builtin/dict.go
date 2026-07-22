package builtin

import (
	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

// dictOp: dict(k1, v1, k2, v2, ...) -> base —— dict 字面量 { k1=v1; k2=v2 } 的运行时。
// base 键写入 kind="dict" 类型标记，成员写入平坦键族 base.k（keytree.Member）。
// 值为 nil（如 null 裸名解析结果）时跳过写入——kvspace 中缺席即 null。
type dictOp struct{}
func (dictOp) Call(f *op.Frame) error {
	inputs := readInputs(f)
	fp := keytree.FrameRoot(f.PC)
	var pairs []kvspace.KVPair
	for _, w := range f.Inst.Writes {
		outKey := resolveWriteKey(f.KV, fp, w)
		pairs = append(pairs, kvspace.KVPair{outKey, kvspace.Dict()})
		for i := 0; i+1 < len(inputs); i += 2 {
			if inputs[i+1].IsNil() { continue }
			pairs = append(pairs, kvspace.KVPair{keytree.Member(outKey, inputs[i].Str()), inputs[i+1]})
		}
	}
	if len(pairs) > 0 { f.KV.Set(pairs) }
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

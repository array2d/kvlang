package builtin

import (
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
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
	for _, w := range f.Inst.Writes {
		outKey := resolveWriteKey(fp, w)
		if err := f.KV.Set(outKey, kvspace.Dict()); err != nil { return err }
		for i := 0; i+1 < len(inputs); i += 2 {
			if inputs[i+1].IsNil() { continue }
			if err := f.KV.Set(keytree.Member(outKey, inputs[i].Str()), inputs[i+1]); err != nil { return err }
		}
	}
	vthread.Set(bg, f.KV, f.Vtid, op.NextPC(f.PC), "running")
	return nil
}

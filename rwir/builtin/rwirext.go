package builtin

import (
	"context"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/rwir"
)

// RunBatch 从 pc 起连续执行 opcode ∈ ops 的 rwir，返回下一条非己方指令的 PC。
// exec 接收已解码 inst（避免重复 Decode）；己方 rwir 是叶算子，不跨帧，linkBase 恒定。
func RunBatch(ctx context.Context, kv kvspace.KVSpace, pc string, ops map[string]bool, exec func(ctx context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir)) string {
	linkBase := keytree.Stack(keytree.FrameRoot(pc))
	for {
		inst, err := rwir.Decode(ctx, kv, linkBase, pc)
		if err != nil || inst.Opcode == "" || !ops[inst.Opcode] {
			break
		}
		exec(ctx, kv, pc, inst)
		pc = rwir.NextPC(pc)
	}
	return pc
}

// FinishBatch 把最终 PC 写回 VM 的 pc key（VM 从该 PC 继续执行）。
func FinishBatch(kv kvspace.KVSpace, vtid, finalPC string) {
	kv.Set([]kvspace.KVPair{{Key: keytree.VThreadPC(vtid), Val: kvspace.NewCharByte([]byte(finalPC)...)}})
}

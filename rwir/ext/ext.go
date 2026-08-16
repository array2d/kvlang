// Package ext 定义 rwirext 扩展运行时框架：一个扩展运行时等价于「只解析己方 opcode
// 的迷你 kvcpu」。中央 kvlang runtime 把控制权交给它，它批量执行己方 rwir 直到遇到
// 非己方指令，再把最终 PC 写回 /vthread/<vtid>/pc。
//
// 一个 rwirext 只需做三件事：
//  1. 用 Ext 声明己方 rwir（opcode + 签名 + 读写参数量）；
//  2. Register/Serve 两行转发（注册签名、常驻监控 .todo、批量执行、交还 PC）；
//  3. Exec 按 opcode 分发到具体 handler。
//
// 参考实现见 rwirext/json 与 rwirext/term。
package ext

import (
	"context"
	"strings"
	"time"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/rwir"
)

// Op 定义一个 rwir：opcode + 签名 + 读写参数量。
type Op struct {
	Name string // opcode，如 "json.to" / "print"
	Sig  string // 签名展示串
	Nr   int32  // 读参数量
	Nw   int32  // 写参数量
}

// Ext 定义一个 rwirext 扩展运行时：己方 opcode 集合 + 执行回调。
type Ext struct {
	Ops  []Op
	Exec func(ctx context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir)
}

// globalRwir 记录全局 rwir（不随 lib 命名空间），由扩展运行时注册。
var globalRwir = map[string]bool{}

// RegisterGlobalRwir 标记 opcode 为全局 rwir（layout 不补 pkg 前缀）。
func RegisterGlobalRwir(opcode string) { globalRwir[opcode] = true }

// IsGlobalRwir 判断 opcode 是否为全局 rwir。
func IsGlobalRwir(opcode string) bool { return globalRwir[opcode] }

// Register 注册所有 rwir 签名到 /rwir/<opcode>（kind=rwir）并标记全局，幂等。
func (e Ext) Register(kv kvspace.KVSpace) {
	pairs := make([]kvspace.KVPair, 0, len(e.Ops))
	for _, o := range e.Ops {
		pairs = append(pairs, kvspace.KVPair{Key: keytree.Rwir(o.Name), Val: kvspace.NewRwir(o.Nr, o.Nw, o.Sig)})
		RegisterGlobalRwir(o.Name)
	}
	kv.Set(pairs)
}

// Serve 常驻循环：注册 + 监控各 .todo<vid> + 批量执行己方 rwir。
func (e Ext) Serve(kv kvspace.KVSpace) {
	ops := map[string]bool{}
	for _, o := range e.Ops {
		ops[o.Name] = true
	}
	for {
		e.Register(kv) // 幂等重注册，兜底外部 FLUSHALL 清空 /rwir
		for _, o := range e.Ops {
			e.serve(kv, o.Name, ops)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

// serve 处理某 opcode 的 .todo<vid>：读 "pc|id"，批量执行，交还最终 PC。
func (e Ext) serve(kv kvspace.KVSpace, op string, ops map[string]bool) {
	ctx := context.Background()
	base := keytree.Rwir(op)
	for _, child := range kv.List(base+"/", false, false) {
		if !strings.HasPrefix(child, ".todo<") {
			continue
		}
		vid := strings.TrimSuffix(strings.TrimPrefix(child, ".todo<"), ">")
		pcID := kvspace.GetOne(kv, base+"/"+child).ValueString()
		pc, id := pcID, ""
		if i := strings.LastIndex(pcID, "|"); i >= 0 {
			pc, id = pcID[:i], pcID[i+1:]
		}

		finalPC := RunSeq(ctx, kv, pc, ops, e.Exec)
		WriteFinalPC(kv, vid, finalPC)

		kv.Set([]kvspace.KVPair{{Key: base + "/.done<" + vid + ">", Val: kvspace.NewCharByte([]byte(id)...)}})
		kv.Del(base + "/" + child)
	}
}

// RunSeq 从 pc 起逐条顺序执行 opcode ∈ ops 的 rwir，返回下一条非己方指令的 PC。
// 顺序执行、保持 PC 次序——ext 的 rwir 存在顺序依赖，非并行 batch；
// exec 接收已解码 inst（避免重复 Decode）；己方 rwir 是叶算子，不跨帧，linkBase 恒定。
func RunSeq(ctx context.Context, kv kvspace.KVSpace, pc string, ops map[string]bool, exec func(ctx context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir)) string {
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

// WriteFinalPC 把最终 PC 写回 VM 的 pc key（VM 从该 PC 继续执行）。
func WriteFinalPC(kv kvspace.KVSpace, vtid, finalPC string) {
	kv.Set([]kvspace.KVPair{{Key: keytree.VThreadPC(vtid), Val: kvspace.NewCharByte([]byte(finalPC)...)}})
}

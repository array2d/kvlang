// term：print/println/cerr/input 四个 rwir 的常驻外部执行器（rwirext）。
// 复用 term 包的终端传输层（ResolveTerm/WriteTerm/ReadTerm），拿 pc 直接 Decode 读参，零值搬运。
package term

import (
	"context"
	"strings"
	"time"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/rwir"
	"kvlang/rwir/builtin"
)

type op struct {
	name  string
	sig   string
	nosep bool // print：无分隔
	rawnl bool // print：不追加换行
	cerr  bool // 写 stderr
	input bool // 读终端
}

var ops = []op{
	{name: "print", sig: "rwir print(A:any, ...) -> ()", nosep: true, rawnl: true},
	{name: "println", sig: "rwir println(A:any, ...) -> ()"},
	{name: "cerr", sig: "rwir cerr(A:any, ...) -> ()", cerr: true},
	{name: "input", sig: "rwir input(prompt:charbyte?) -> (C:charbyte)", input: true},
}

var opSet = map[string]bool{}
var opByOpcode = map[string]op{}

// init 在包导入时标记这些 opcode 为全局 rwir（layout 不补 pkg 前缀）。
func init() {
	for _, o := range ops {
		builtin.RegisterGlobalRwir(o.name)
		opSet[o.name] = true
		opByOpcode[o.name] = o
	}
}

// Register 把 term 承载的 rwir 注册到 /rwir/（kind=rwir），并设置默认终端，幂等。
func Register(kv kvspace.KVSpace) {
	registerDefaultTerm(kv)
	pairs := make([]kvspace.KVPair, 0, len(ops))
	for _, o := range ops {
		nr, nw := int32(1), int32(0)
		if o.input {
			nw = 1
		}
		pairs = append(pairs, kvspace.KVPair{
			Key: keytree.Rwir(o.name),
			Val: kvspace.NewRwir(nr, nw, o.sig),
		})
	}
	kv.Set(pairs)
}

// registerDefaultTerm 设置默认终端 kvlangrun → file /dev/stdout|stderr|stdin。
func registerDefaultTerm(kv kvspace.KVSpace) {
	h := keytree.DevTTY("kvlangrun", "")
	kvspace.MkIndexRecursive(kv, h+"stdout/")
	kvspace.MkIndexRecursive(kv, h+"stderr/")
	kvspace.MkIndexRecursive(kv, h+"stdin/")
	kv.Set([]kvspace.KVPair{
		{Key: h + "stdout/type", Val: kvspace.NewCharByte([]byte("file")...)},
		{Key: h + "stdout/detail", Val: kvspace.NewCharByte([]byte("/dev/stdout")...)},
		{Key: h + "stderr/type", Val: kvspace.NewCharByte([]byte("file")...)},
		{Key: h + "stderr/detail", Val: kvspace.NewCharByte([]byte("/dev/stderr")...)},
		{Key: h + "stdin/type", Val: kvspace.NewCharByte([]byte("file")...)},
		{Key: h + "stdin/detail", Val: kvspace.NewCharByte([]byte("/dev/stdin")...)},
	})
}

// Serve 常驻循环：持续处理各 rwir 的 .todo<vid>。
func Serve(kv kvspace.KVSpace) {
	for {
		Register(kv) // 幂等重注册，兜底外部 FLUSHALL 清空 /rwir
		for _, o := range ops {
			serveOne(kv, o)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func serveOne(kv kvspace.KVSpace, o op) {
	ctx := context.Background()
	base := keytree.Rwir(o.name)
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

		// 批量执行己方 rwir，直到下一条非己方指令
		finalPC := builtin.RunBatch(ctx, kv, pc, opSet, exec)
		builtin.FinishBatch(kv, vid, finalPC)

		doneKey := base + "/.done<" + vid + ">"
		kv.Set([]kvspace.KVPair{{Key: doneKey, Val: kvspace.NewCharByte([]byte(id)...)}})
		kv.Del(base + "/" + child)
	}
}

func exec(_ context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	o := opByOpcode[inst.Opcode]
	if o.input {
		handleInput(kv, pc, inst)
	} else {
		handlePrint(kv, o, pc, inst)
	}
}

func handlePrint(kv kvspace.KVSpace, o op, pc string, inst *rwir.Rwir) {
	vtid := keytree.VtidFromPC(pc)
	parts := make([]string, len(inst.Reads))
	for i, r := range inst.Reads {
		parts[i] = builtin.Display(builtin.ResolveReadValue(kv, keytree.FrameRoot(pc), r))
	}
	sep := " "
	if o.nosep {
		sep = ""
	}
	line := strings.Join(parts, sep)

	stream := "stdout"
	if o.cerr {
		stream = "stderr"
	}
	ts := ResolveTerm(kv, vtid, stream)
	if ts.IsZero() {
		return
	}
	if o.rawnl {
		WriteTermRaw(ts, line)
	} else {
		WriteTerm(ts, line)
	}
}

func handleInput(kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	vtid := keytree.VtidFromPC(pc)
	if len(inst.Reads) > 0 {
		v := builtin.ResolveReadValue(kv, keytree.FrameRoot(pc), inst.Reads[0])
		if prompt := builtin.Display(v); prompt != "" {
			ts := ResolveTerm(kv, vtid, "stdout")
			if !ts.IsZero() {
				WriteTerm(ts, prompt)
			}
		}
	}
	ts := ResolveTerm(kv, vtid, "stdin")
	if ts.IsZero() {
		return
	}
	val, _ := ReadTerm(ts)
	// 直接写回 input 调用处的写槽（pc 的写参），不经过 .result 中转
	if len(inst.Writes) > 0 {
		writeKey := builtin.ResolveWriteSlot(kv, keytree.FrameRoot(pc), inst.Writes[0].Name)
		kv.Set([]kvspace.KVPair{{Key: writeKey, Val: kvspace.NewCharByte([]byte(val)...)}})
	}
}

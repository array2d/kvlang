// Package term 是 rwirext 扩展运行时：print/println/cerr/input 四个 rwir。
// 只需声明己方 op（含各自行为标志），交给 ext.Ext 框架注册与常驻执行。
// 自行解释执行：直接写 /dev/stdout|stderr、读 /dev/stdin，无终端发现。
package term

import (
	"context"
	"strings"

	"github.com/array2d/kvspace-go"
	"oldhero/keytree"
	"oldhero/rwir"
	"oldhero/rwir/builtin"
	"oldhero/rwir/ext"
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

var rt = ext.Ext{Ops: toOps(), Exec: exec}

var opByOpcode = map[string]op{}

// init 在包导入时标记全局 rwir：term 与中央 runtime 同进程，vet/format 也会 layout
// 用户代码但不调 Register，必须在导入期就注册，否则裸 opcode 会被补 pkg 前缀。
func init() {
	for _, o := range ops {
		ext.RegisterGlobalRwir(o.name)
		opByOpcode[o.name] = o
	}
}

func toOps() []ext.Op {
	out := make([]ext.Op, len(ops))
	for i, o := range ops {
		nr, nw := int32(1), int32(0)
		if o.input {
			nw = 1
		}
		out[i] = ext.Op{Name: o.name, Sig: o.sig, Nr: nr, Nw: nw}
	}
	return out
}

func Register(kv kvspace.KVSpace) { rt.Register(kv) }
func Serve(kv kvspace.KVSpace)    { rt.Serve(kv) }

func exec(_ context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	o := opByOpcode[inst.Opcode]
	if o.input {
		handleInput(kv, pc, inst)
	} else {
		handlePrint(kv, o, pc, inst)
	}
}

func handlePrint(kv kvspace.KVSpace, o op, pc string, inst *rwir.Rwir) {
	parts := make([]string, len(inst.Reads))
	for i, r := range inst.Reads {
		parts[i] = builtin.Display(builtin.ResolveReadValue(kv, keytree.FrameRoot(pc), r))
	}
	sep := " "
	if o.nosep {
		sep = ""
	}
	line := strings.Join(parts, sep)

	path := "/dev/stdout"
	if o.cerr {
		path = "/dev/stderr"
	}
	if o.rawnl {
		writeFileRaw(path, line)
	} else {
		writeFile(path, line)
	}
}

func handleInput(kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	if len(inst.Reads) > 0 {
		v := builtin.ResolveReadValue(kv, keytree.FrameRoot(pc), inst.Reads[0])
		if prompt := builtin.Display(v); prompt != "" {
			writeFile("/dev/stdout", prompt)
		}
	}
	val, _ := readFile("/dev/stdin")
	if len(inst.Writes) > 0 {
		writeKey := builtin.ResolveWriteSlot(kv, keytree.FrameRoot(pc), inst.Writes[0].Name)
		kv.Set([]kvspace.KVPair{{Key: writeKey, Val: kvspace.NewCharByte([]byte(val)...)}})
	}
}

// Package layoutrwir 将 AST 布局到 KV 空间的执行层。
//
// 存储约定：
//
//	/src/<pkg>/<name>              函数完整源码文本
//	/func/<pkg>/<name>             编译后签名（FuncSig.String()）
//	/func/<pkg>/<name>/[i,j]       编译后指令
//	/func/<pkg>/<name>/<label>/    基本块子路径
//	/func/idx/<name>               函数名 → pkg 反向索引
//
// 帧模型（Link P4）：
//
//	callPC                         调用指令绝对路径，作为帧根
//	frameRoot/.funclib                     软链接 → /lib/<pkg>/<name>（只读指令），keytree.FuncLib(frameRoot)
//	frameRoot/<param>                 参数（本帧局部变量，不经过链接）
//	frameRoot/.callpc                 存储 callPC 自身（keytree.CallPC），HandleReturn 据此恢复父 PC
//	frameRoot/.rootfunc               根函数名（keytree.RootFunc），TCO 不更新，供 resolveLabel 使用
//
// 写槽（读写码语义）：
//
//	kvlang 只有读写码，函数调用 `add(x,y) -> ./s` 的 `-> ./s` 是**调用方指定的写目标**。
//	写槽路径已存在调用指令 [addr0,1], [addr0,2],... 中，HandleReturn 直接从 .callpc
//	推导路径读取，无需在子帧额外存储——没有"返回值"，只有写槽绑定。
package layoutrwir

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/op"
	"kvlang/internal/op/builtin"
	"kvlang/internal/parser"
	"kvlang/internal/vthread"
)

// WriteBody 将 []Stmt 写入 /lib/<pkg>/<name>/ 下的结构化 KV（编译后指令）。
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt) {
	prefix := keytree.LibFunc(pkg, name)
	idx := 0
	for _, st := range body {
		writeStmt(kv, st, prefix, &idx)
	}
}

// writeStmt 将单条 Stmt 写入 KV 空间。
// lower.File 保证调用时只剩 *Instruction 和 *BlockStmt，其余类型无操作。
func writeStmt(kv kvspace.KVSpace, st ast.Stmt, prefix string, idx *int) {
	switch s := st.(type) {
	case *ast.Instruction:
		n := *idx
		opcode, reads := s.Flat()
		if opcode != "" {
			kv.Set(fmt.Sprintf("%s/[%d,0]", prefix, n), kvspace.Str(opcode))
		}
		for j, r := range reads {
			kv.Set(fmt.Sprintf("%s/[%d,-%d]", prefix, n, j+1), kvspace.Str(r))
		}
		for j, w := range s.Writes {
			kv.Set(fmt.Sprintf("%s/[%d,%d]", prefix, n, j+1), kvspace.Str(w))
		}
		*idx = n + 1
	case *ast.BlockStmt:
		sub := prefix + "/" + s.Label
		i := 0
		for _, child := range s.Body {
			writeStmt(kv, child, sub, &i)
		}
	}
}

// HandleCall 执行 CALL：链接函数指令树，绑定参数，存储写槽。
//
// pc 为调用指令的绝对路径（如 /vthread/42/.funclib/[3,0]）。
// tail=true 时执行 TCO：复用当前帧，仅重链 .funclib（br/goto 路径）。
// 返回被调帧第一条指令的绝对 PC；失败时返回 ""（不再返回 pc，避免"PC == 失败"歧义）。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction, tail bool) string {
	vtid := keytree.VtidFromPC(pc)
	funcName := inst.Reads[0]

	var pkg string
	// 全路径调用（fix-039）：/lib/math/sum → pkg=math, name=sum；
	// /lib/a/b/sum → pkg=a/b, name=sum；/lib/sum → pkg="", name=sum
	if strings.HasPrefix(funcName, "/lib/") {
		rest := funcName[len("/lib/"):]
		// /lib/aaa/bbb/math.sum → pkg=aaa/bbb/math, name=sum
		if dot := strings.LastIndex(rest, "."); dot > 0 {
			pkg = rest[:dot]
			funcName = rest[dot+1:]
		} else {
			funcName = rest
			pkg = ""
		}
	} else {
		pkgVal, err := kv.Get(keytree.LibIdx(funcName))
		if err != nil || pkgVal.IsNil() {
			vthread.SetError(ctx, kv, vtid, pc, "NameError: func not found: "+funcName)
			return ""
		}
		pkg = pkgVal.Str()
	}
	funcKey := keytree.LibFunc(pkg, funcName)

	sigVal, err := kv.Get(funcKey)
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "NameError: func signature not found: "+funcName)
		return ""
	}
	funcSig := parser.ParseFuncSig(sigVal.Str())

	// 参数不可同名（fix-032：parser 已拦截，VM 兜底 agent 直写 KV 构造的非法签名）
	if err := checkDupParams(funcSig, funcName); err != "" {
		vthread.SetError(ctx, kv, vtid, pc, err)
		return ""
	}

	// TCO：复用当前帧，仅重链 .funclib 到目标块代码区（.rootfunc 不更新，保持根函数名）
	if tail {
		frameRoot := keytree.FrameRoot(pc)
		kv.Unlink(keytree.FuncLib(frameRoot))
		if err := kv.Link(funcKey, keytree.FuncLib(frameRoot)); err != nil {
			vthread.SetError(ctx, kv, vtid, pc, "tco RuntimeError: link failed: "+err.Error())
			return ""
		}
		return keytree.FuncLib(frameRoot) + "/[0,0]"
	}

	// 普通 call：从调用方帧解析实参后绑定到子帧
	callerFrameRoot := keytree.FrameRoot(pc)
	frameRoot := keytree.ChildFrameRoot(pc)

	// 链接只读指令区：frameRoot/.funclib → /lib/<pkg>/<name>
	if err := kv.Link(funcKey, keytree.FuncLib(frameRoot)); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "RuntimeError: link failed: "+err.Error())
		return ""
	}

	// 存储帧元数据
	kv.Set(keytree.CallPC(frameRoot), kvspace.Str(pc))
	kv.Set(keytree.RootFunc(frameRoot), kvspace.Str(funcName))

	// 读参零拷贝：存储调用方值的绝对路径，CPU 直接从此路径读取
	params := funcSig.ParamNames()
	for i, param := range params {
		if i+1 < len(inst.Reads) {
			rk := frameSlotKey(callerFrameRoot, inst.Reads[i+1])
			if rk != "" {
				kv.Set(keytree.RParam(frameRoot, param), kvspace.Str(rk))
			}
		}
	}
	// 写参零拷贝：存储调用方写目标的绝对路径，CPU 直接写入此路径
	for i, ret := range funcSig.Returns {
		wSlotPath := op.WriteSlotPC(pc, i)
		wTargetVal, _ := kv.Get(wSlotPath)
		wTarget := wTargetVal.Str()
		if wTarget != "" {
			wk := frameSlotKey(callerFrameRoot, wTarget)
			if wk != "" {
				kv.Set(keytree.WParam(frameRoot, ret.Name), kvspace.Str(wk))
			}
		}
	}
	if len(params) > 0 {
		kv.Set(keytree.FrameRO(frameRoot), kvspace.Str(strings.Join(params, ",")))
	}

	return keytree.FuncLib(frameRoot) + "/[0,0]"
}

// HandleReturn 处理 RETURN：将被调方输出写入父帧写槽，清理帧，恢复父帧 PC。
//
// pc 为 return 指令的绝对路径（如 /vthread/42/[3,0]/.funclib/[1,0]）。
// 返回值：
//
//	("", retVal) — 顶层 return（frameRoot == vthreadRoot）；retVal 供 vthread.SetDone
//	(nextPC, "") — 嵌套 return；nextPC 为父帧 callPC 的下一条指令
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction) (nextPC, retVal string) {
	vtid := keytree.VtidFromPC(pc)
	vthreadRoot := keytree.VThread(vtid)

	frameRoot := keytree.FrameRoot(pc)

	// 读取本帧第一个返回值（供顶层 return 用）
	var returnValue string
	if len(inst.Reads) > 0 {
		if rk := frameSlotKey(frameRoot, inst.Reads[0]); rk != "" {
			v, _ := kv.Get(rk)
			returnValue = v.Str()
		}
	}

	// 顶层 return：frameRoot 就是 vthreadRoot
	if frameRoot == vthreadRoot {
		return "", returnValue
	}

	// 读取 callPC（在 cleanup 前）
	callPCVal, _ := kv.Get(keytree.CallPC(frameRoot))
	callPC := callPCVal.Str()

	// 写参已在子帧执行期间通过 .wparam 零拷贝直写父帧，HandleReturn 无需搬运。

	// 清理帧
	kv.Unlink(keytree.FuncLib(frameRoot))
	kv.DelTree(frameRoot)

	if callPC == "" {
		return "", ""
	}
	return op.NextPC(callPC), ""
}

// RegisterBlocks 为函数体内所有 BlockStmt label 注册编译后子函数签名。
// 写入 /lib/<pkg>/<name>/<label>，供 br/goto 运行时查找。
func RegisterBlocks(kv kvspace.KVSpace, pkg, parent string, body []ast.Stmt) {
	for _, st := range body {
		if b, ok := st.(*ast.BlockStmt); ok {
			blockKey := keytree.LibFunc(pkg, parent+"/"+b.Label)
			kv.Set(blockKey, kvspace.Str("def "+b.Label+"() -> ()"))
			kv.Set(keytree.LibIdx(parent+"/"+b.Label), kvspace.Str(pkg))
			RegisterBlocks(kv, pkg, parent+"/"+b.Label, b.Body)
		}
	}
}

// Bootstrap 为 vthread 的顶层入口函数建立初始帧，无需父帧（无 .callpc）。
//
// 与 HandleCall 的区别：Bootstrap 直接在 vthreadRoot 建帧；HandleCall 建子帧。
// 顶层帧的特征：frameRoot == vthreadRoot → HandleReturn 识别为顶层 return → SetDone。
//
// args 为按序传入的参数值（对应 funcSig.ParamNames()），可为空。
// 成功返回第一条指令的绝对 PC（vthreadRoot/.funclib/[0,0]）；失败返回 ""。
func Bootstrap(ctx context.Context, kv kvspace.KVSpace, vtid, funcName string, args []string) string {
	pkgVal, err := kv.Get(keytree.LibIdx(funcName))
	pkg := pkgVal.Str()
	if err != nil || pkgVal.IsNil() {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: NameError: func not found: "+funcName)
		return ""
	}
	funcKey := keytree.LibFunc(pkg, funcName)

	vthreadRoot := keytree.VThread(vtid)
	if err := kv.Link(funcKey, keytree.FuncLib(vthreadRoot)); err != nil {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: RuntimeError: link failed: "+err.Error())
		return ""
	}
	kv.Set(keytree.RootFunc(vthreadRoot), kvspace.Str(funcName))

	// 绑定入参（若有）。顶层帧无调用方，值写入 vthreadRoot，.rparam 指向自身槽位。
	if len(args) > 0 {
		sigVal, _ := kv.Get(funcKey)
		sig := parser.ParseFuncSig(sigVal.Str())
		if err := checkDupParams(sig, funcName); err != "" {
			vthread.SetError(ctx, kv, vtid, "", err)
			return ""
		}
		params := sig.ParamNames()
		for i, param := range params {
			if i < len(args) {
				dest := vthreadRoot + "/" + param; kv.Set(dest, builtin.ResolveReadValue(kv, "", args[i])); kv.Set(keytree.RParam(vthreadRoot, param), kvspace.Str(dest))
			}
		}
		if len(params) > 0 {
			kv.Set(keytree.FrameRO(vthreadRoot), kvspace.Str(strings.Join(params, ","))) // fix-027
		}
	}

	return keytree.FuncLib(vthreadRoot) + "/[0,0]"
}

// WriteFunc 完成一个函数的全部 KV 写入：
//  0. 先 DelTree 清除旧函数数据（避免旧指令 slot 污染新布局）
//  1. 源码写入 /src/<pkg>/<name>
//  2. 编译签名写入 /lib/<pkg>/<name>
//  3. 编译 body 指令写入 /lib/<pkg>/<name>/[i,j]
//  4. 块标签写入 /lib/<pkg>/<name>/<label>/
//  5. 反向索引写入 /lib/idx/<name>
// frameSlotKey 将槽表达式转换为绝对 KV 路径。
//
//	"x"    → frameRoot + "/x"   (裸标识符)
//	"/abs" → "/abs"             (绝对路径，直通)
//	""     → ""                 (空，忽略)
//	".xxx" → ""                 (引擎保留键，忽略，如 ._ 丢弃槽)
func frameSlotKey(frameRoot, slot string) string {
	if slot == "" {
		return ""
	}
	if slot[0] == '/' {
		return slot // 绝对路径直通
	}
	if slot[0] == '.' {
		return "" // 引擎保留键（._ 等丢弃槽），不写
	}
	return frameRoot + "/" + slot // 裸标识符
}

func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	// 清除旧函数数据：旧指令的读/写槽数量可能多于新函数，
	// 若不清除则旧槽（如 [0,-1]、[0,-2]）会被新执行错误读取。
	kv.DelTree(keytree.LibFunc(pkg, fn.Sig.Name))
	kv.Set(keytree.Src(pkg, fn.Sig.Name), kvspace.Str(fn.FullText()))
	kv.Set(keytree.LibSrc(pkg, fn.Sig.Name), kvspace.Str(fn.FullText())) // fix-034: /lib/<pkg>/<name>.src
	kv.Set(keytree.LibFunc(pkg, fn.Sig.Name), kvspace.Str(fn.Sig.String()))
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body)
	RegisterBlocks(kv, pkg, fn.Sig.Name, fn.Body)
	kv.Set(keytree.LibIdx(fn.Sig.Name), kvspace.Str(pkg))
}

// checkDupParams 参数不可同名（fix-032：VM 运行时兜底，parser 已拦截源码路径）。
func checkDupParams(sig ast.FuncSig, funcName string) string {
	seen := map[string]bool{}
	for _, p := range sig.ParamNames() {
		if seen[p] { return fmt.Sprintf("func %s: duplicate read-param %q", funcName, p) }
		seen[p] = true
	}
	for _, r := range sig.Returns {
		if seen[r.Name] { return fmt.Sprintf("func %s: param %q appears in both read-params and write-params", funcName, r.Name) }
		seen[r.Name] = true
	}
	return ""
}

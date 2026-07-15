// Package layoutcode 将 AST 布局到 KV 空间的执行层。
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
//	callPC/_fn                     软链接 → /func/<pkg>/<name>（只读指令）
//	callPC/<param>                 参数（本帧局部变量，不经过链接）
//	callPC/.callpc                 存储 callPC 自身，供 HandleReturn 恢复
//	callPC/.ret0                   调用方的写槽（return 时写入父帧）
//	callPC/.rootfunc               根函数名（TCO 不更新，供 resolveLabel 使用）
package layoutcode

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
	"kvlang/internal/op/builtin"
	"kvlang/internal/parser"
	"kvlang/internal/vthread"
)

// WriteBody 将 []Stmt 写入 /func/<pkg>/<name>/ 下的结构化 KV（编译后指令）。
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt) {
	prefix := keytree.Func(pkg, name)
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

// HandleCall 执行 CALL：链接函数指令树，绑定参数，存储返回槽。
//
// pc 为调用指令的绝对路径（如 /vthread/42/_fn/[3,0]）。
// tail=true 时执行 TCO：复用当前帧，仅重链 _fn（br/goto 路径）。
// 返回被调帧第一条指令的绝对 PC；失败时返回 ""（不再返回 pc，避免"PC == 失败"歧义）。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction, tail bool) string {
	vtid := keytree.VtidFromPC(pc)
	funcName := inst.Reads[0]

	pkgVal, err := kv.Get(keytree.FuncIdx(funcName))
	pkg := pkgVal.Str()
	if err != nil || pkg == "" {
		vthread.SetError(ctx, kv, vtid, pc, "func not found: "+funcName)
		return ""
	}
	funcKey := keytree.Func(pkg, funcName)

	sigVal, err := kv.Get(funcKey)
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "func signature not found: "+funcName)
		return ""
	}
	funcSig := parser.ParseFuncSig(sigVal.Str())

	// TCO：复用当前帧，仅重链 _fn 到目标块代码区（.rootfunc 不更新，保持根函数名）
	if tail {
		frameRoot := keytree.FrameRoot(pc)
		kv.Unlink(keytree.FnCode(frameRoot))
		if err := kv.Link(funcKey, keytree.FnCode(frameRoot)); err != nil {
			vthread.SetError(ctx, kv, vtid, pc, "tco link failed: "+err.Error())
			return ""
		}
		return keytree.FnCode(frameRoot) + "/[0,0]"
	}

	// 普通 call：从调用方帧解析实参后绑定到子帧
	callerFrameRoot := keytree.FrameRoot(pc)
	frameRoot := keytree.ChildFrameRoot(pc)

	// 链接只读指令区：frameRoot/_fn → /func/<pkg>/<name>
	if err := kv.Link(funcKey, keytree.FnCode(frameRoot)); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "link failed: "+err.Error())
		return ""
	}

	// 存储帧元数据
	kv.Set(frameRoot+"/.callpc", kvspace.Str(pc))
	kv.Set(frameRoot+"/.rootfunc", kvspace.Str(funcName))

	// 绑定参数：从调用方帧解析实参值后写入子帧（不经过链接）
	for i, param := range funcSig.ParamNames() {
		if i+1 < len(inst.Reads) {
			kv.Set(frameRoot+"/"+param, builtin.ResolveReadValue(kv, callerFrameRoot, inst.Reads[i+1]))
		}
	}

	// 存储调用方写槽（return 时写入父帧）
	for i := range funcSig.ReturnNames() {
		if i < len(inst.Writes) {
			kv.Set(fmt.Sprintf("%s/.ret%d", frameRoot, i), kvspace.Str(inst.Writes[i]))
		}
	}

	return keytree.FnCode(frameRoot) + "/[0,0]"
}

// HandleReturn 处理 RETURN：回传值，清理帧，恢复父帧 PC。
//
// pc 为 return 指令的绝对路径（如 /vthread/42/[3,0]/_fn/[1,0]）。
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
	if len(inst.Reads) > 0 && strings.HasPrefix(inst.Reads[0], "./") {
		v, _ := kv.Get(frameRoot + "/" + inst.Reads[0][2:])
		returnValue = v.Str()
	}

	// 顶层 return：frameRoot 就是 vthreadRoot
	if frameRoot == vthreadRoot {
		return "", returnValue
	}

	// 读取 callPC（在 cleanup 前）
	callPCVal, _ := kv.Get(frameRoot + "/.callpc")
	callPC := callPCVal.Str()

	// 将所有返回值写入父帧的对应写槽（.ret0, .ret1, ...）
	if callPC != "" {
		parentFrameRoot := keytree.FrameRoot(callPC)
		for i, read := range inst.Reads {
			retKey := fmt.Sprintf("%s/.ret%d", frameRoot, i)
			retTargetVal, _ := kv.Get(retKey)
			retTarget := retTargetVal.Str()
			if strings.HasPrefix(retTarget, "./") {
				v, _ := kv.Get(frameRoot + "/" + read[2:])
				kv.Set(parentFrameRoot+"/"+retTarget[2:], v)
			}
		}
	}

	// 清理帧：先 Unlink 代码区，再 DelTree 帧根（params / .ret0 / .callpc / .rootfunc）
	kv.Unlink(keytree.FnCode(frameRoot))
	kv.DelTree(frameRoot)

	if callPC == "" {
		return "", ""
	}
	return op.NextPC(callPC), ""
}

// RegisterBlocks 为函数体内所有 BlockStmt label 注册编译后子函数签名。
// 写入 /func/<pkg>/<name>/<label>，供 br/goto 运行时查找。
func RegisterBlocks(kv kvspace.KVSpace, pkg, parent string, body []ast.Stmt) {
	for _, st := range body {
		if b, ok := st.(*ast.BlockStmt); ok {
			blockKey := keytree.Func(pkg, parent+"/"+b.Label)
			kv.Set(blockKey, kvspace.Str("def "+b.Label+"() -> ()"))
			kv.Set(keytree.FuncIdx(parent+"/"+b.Label), kvspace.Str(pkg))
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
// 成功返回第一条指令的绝对 PC（vthreadRoot/_fn/[0,0]）；失败返回 ""。
func Bootstrap(ctx context.Context, kv kvspace.KVSpace, vtid, funcName string, args []string) string {
	pkgVal, err := kv.Get(keytree.FuncIdx(funcName))
	pkg := pkgVal.Str()
	if err != nil || pkg == "" {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: func not found: "+funcName)
		return ""
	}
	funcKey := keytree.Func(pkg, funcName)

	vthreadRoot := keytree.VThread(vtid)
	if err := kv.Link(funcKey, keytree.FnCode(vthreadRoot)); err != nil {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: link failed: "+err.Error())
		return ""
	}
	kv.Set(vthreadRoot+"/.rootfunc", kvspace.Str(funcName))

	// 绑定入参（若有）
	if len(args) > 0 {
		sigVal, _ := kv.Get(funcKey)
		sig := parser.ParseFuncSig(sigVal.Str())
		for i, param := range sig.ParamNames() {
			if i < len(args) {
				kv.Set(vthreadRoot+"/"+param, kvspace.Str(args[i]))
			}
		}
	}

	return keytree.FnCode(vthreadRoot) + "/[0,0]"
}

// WriteFunc 完成一个函数的全部 KV 写入：
//  0. 先 DelTree 清除旧函数数据（避免旧指令 slot 污染新布局）
//  1. 源码写入 /src/<pkg>/<name>
//  2. 编译签名写入 /func/<pkg>/<name>
//  3. 编译 body 指令写入 /func/<pkg>/<name>/[i,j]
//  4. 块标签写入 /func/<pkg>/<name>/<label>/
//  5. 反向索引写入 /func/idx/<name>
func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	// 清除旧函数数据：旧指令的读/写槽数量可能多于新函数，
	// 若不清除则旧槽（如 [0,-1]、[0,-2]）会被新执行错误读取。
	kv.DelTree(keytree.Func(pkg, fn.Sig.Name))
	kv.Set(keytree.Src(pkg, fn.Sig.Name), kvspace.Str(fn.FullText()))
	kv.Set(keytree.Func(pkg, fn.Sig.Name), kvspace.Str(fn.Sig.String()))
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body)
	RegisterBlocks(kv, pkg, fn.Sig.Name, fn.Body)
	kv.Set(keytree.FuncIdx(fn.Sig.Name), kvspace.Str(pkg))
}

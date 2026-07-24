// Package layoutrwir 将 AST 布局到 KV 空间的执行层。
//
// 存储约定：
//
//	/lib/<pkg>.<name>               编译后签名（FuncSig.String()）
//	/lib/<pkg>.<name>/[i,j]         编译后指令
//	/lib/<pkg>.<name>/<label>/      基本块子路径
//	/lib/<pkg>.<name>.src           源码副本（fix-034）
//
// 帧模型（callPC = 子帧根；帧根本身是 extindex 指向 /lib/）：
//
//	callPC = parentFrame/[coord]             调用指令 PC = 子帧根
//	frameRoot/                              extindex → /lib/<pkg>/<name>/（指令查找）
//	frameRoot.x                             局部变量（extindex 写层）
//	frameRoot.rparam/<name> /.wparam/<name>  零拷贝读写参重定向
//
// kvcpu 取指：linkBase = Stack(FrameRoot(pc))，即帧根目录；
// kvspace 的 extindex 机制自动先查本地层再回落 /lib/。
package layoutrwir

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/internal/lower"
	"kvlang/internal/op"
	"kvlang/internal/op/builtin"
	"kvlang/internal/parser"
	"kvlang/internal/vthread"
)

// WriteBody 将 []Stmt 写入 /lib/<pkg>/<name>/ 下的结构化 KV（编译后指令）。
// pkg 非空时，用户函数调用 opcode 自动限定为 pkg.name（替代 idx 反向索引）。
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt, typeMap map[string]string) {
	prefix := keytree.LibFunc(pkg, name)
	idx := 0
	for _, st := range body {
		writeStmt(kv, st, prefix, &idx, typeMap, pkg)
	}
}

// writeStmt 将单条 Stmt 写入 KV 空间。
// lower.File 保证调用时只剩 *Instruction 和 *BlockStmt，其余类型无操作。
func writeStmt(kv kvspace.KVSpace, st ast.Stmt, prefix string, idx *int, typeMap map[string]string, pkg string) {
	switch s := st.(type) {
	case *ast.Instruction:
		n := *idx
		for j, w := range s.Writes {
			if j < len(s.WriteTypes) && s.WriteTypes[j] != "" {
				typeMap[w] = s.WriteTypes[j]
			}
		}
		opcode, reads := s.Flat()
		// 同包调用限定：pkg 非空且 opcode 是用户函数（非 builtin、非控制流、非已限定）
		if pkg != "" && !builtin.IsNativeOp(opcode) && !op.IsControlOp(opcode) &&
			!strings.Contains(opcode, keytree.FuncPathSep) && !strings.HasPrefix(opcode, keytree.LibRoot+keytree.PathSegSep) &&
			opcode != "=" {
			opcode = pkg + keytree.FuncPathSep + opcode
		}
		pairs := make([]kvspace.KVPair, 0, 1+len(reads)+len(s.Writes))
		if opcode != "" {
			pairs = append(pairs, kvspace.KVPair{fmt.Sprintf("%s/[%d,0]", prefix, n), slotValue(opcode, typeMap)})
		}
		for j, r := range reads {
			pairs = append(pairs, kvspace.KVPair{fmt.Sprintf("%s/[%d,-%d]", prefix, n, j+1), slotValue(r, typeMap)})
		}
		for j, w := range s.Writes {
			pairs = append(pairs, kvspace.KVPair{fmt.Sprintf("%s/[%d,%d]", prefix, n, j+1), slotValue(w, typeMap)})
		}
		if len(pairs) > 0 {
			kv.Set(pairs)
		}
		*idx = n + 1
	case *ast.BlockStmt:
		sub := prefix + "/" + s.Label
		kvspace.MkIndexRecursive(kv, sub+"/")
		i := 0
		for _, child := range s.Body {
			writeStmt(kv, child, sub, &i, typeMap, pkg)
		}
	}
}

// HandleCall 执行 CALL：链接函数指令树，绑定参数。
//
// pc 为调用指令的绝对路径（如 /vthread/42/[3,0]）。callPC 即子帧根。
// tail=true 时执行 TCO：复用当前帧，Unlink + ExtIndex 重建帧根 extindex。
// 返回被调帧第一条指令 PC（frameRoot/[0,0]）；失败时返回 ""。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction, tail bool) string {
	vtid := keytree.VtidFromPC(pc)
	funcName := inst.Reads[0]

	var pkg string
	libPrefix := keytree.LibRoot + keytree.PathSegSep
	if strings.HasPrefix(funcName, libPrefix) {
		rest := funcName[len(libPrefix):]
		if dot := strings.LastIndex(rest, keytree.FuncPathSep); dot > 0 {
			pkg = rest[:dot]
			funcName = rest[dot+len(keytree.FuncPathSep):]
		} else {
			funcName = rest
		}
	} else if dot := strings.LastIndex(funcName, keytree.FuncPathSep); dot > 0 {
		pkg = funcName[:dot]
		funcName = funcName[dot+len(keytree.FuncPathSep):]
	}
	funcKey := keytree.LibFunc(pkg, funcName)

	sigVal := kvspace.GetOne(kv, funcKey)
	if sigVal.IsNil() {
		vthread.SetError(ctx, kv, vtid, pc, "NameError: func signature not found: "+funcName)
		return ""
	}
	funcSig := parser.ParseFuncSig(sigVal.Str())

	// 参数不可同名（fix-032：parser 已拦截，VM 兜底 agent 直写 KV 构造的非法签名）
	if err := checkDupParams(funcSig, funcName); err != "" {
		vthread.SetError(ctx, kv, vtid, pc, err)
		return ""
	}

	// TCO：复用当前帧，Unlink + ExtIndex 重建帧根 extindex。
	if tail {
		frameRoot := keytree.FrameRoot(pc)
		kv.UnLink(keytree.Stack(frameRoot))
		if err := kv.ExtIndex(keytree.Stack(frameRoot), funcKey+"/"); err != nil {
			vthread.SetError(ctx, kv, vtid, pc, "tco RuntimeError: overlay failed: "+err.Error())
			return ""
		}
		return keytree.EntryPC(frameRoot)
	}

	// 普通 call：callPC 即子帧根
	callerFrameRoot := keytree.FrameRoot(pc)
	frameRoot := pc

	kvspace.MkIndexRecursive(kv, keytree.Stack(frameRoot))
	if err := kv.ExtIndex(keytree.Stack(frameRoot), funcKey+"/"); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "RuntimeError: overlay failed: "+err.Error())
		return ""
	}

	kv.Set([]kvspace.KVPair{
						{frameRoot + keytree.MemberSep + keytree.SegRParam + "/", kvspace.Raw(kvspace.KindIndex, nil)},
		{frameRoot + keytree.MemberSep + keytree.SegWParam + "/", kvspace.Raw(kvspace.KindIndex, nil)},
	})

	// 读参零拷贝：.rparam 重定向到调用方值位置
	// 字面量存为临时变量；变量引用追踪 .rparam 链到源头
	litSeq := 0
	params := funcSig.ParamNames()
	var paramPairs []kvspace.KVPair
	for i, param := range params {
		if i+1 < len(inst.Reads) {
			arg := inst.Reads[i+1]
			rk := resolveReadPath(kv, callerFrameRoot, arg)
			if rk == "" && isLiteral(arg) {
				rk = fmt.Sprintf("%s/._lit%d", callerFrameRoot, litSeq)
				paramPairs = append(paramPairs, kvspace.KVPair{rk, builtin.ResolveReadValue(kv, callerFrameRoot, arg)})
				litSeq++
			}
			if rk != "" {
				paramPairs = append(paramPairs, kvspace.KVPair{keytree.RParam(frameRoot, param), kvspace.Str(rk)})
			}
		}
	}
	if len(paramPairs) > 0 {
		kv.Set(paramPairs)
	}
	// 写参零拷贝：.rparam 和 .wparam 指向同一调用方路径，不创建本地副本
	callerLink := keytree.Stack(callerFrameRoot)
	addr0 := op.ExtractAddr0FromPC(pc)
	for i, ret := range funcSig.Returns {
		wSlot := fmt.Sprintf("%s[%d,%d]", callerLink, addr0, i+1)
		wTargetVal := kvspace.GetOne(kv, wSlot)
		wTarget := string(wTargetVal.RawBytes())
		if wTarget == "" {
			continue
		}
		wk := resolveReadPath(kv, callerFrameRoot, wTarget)
		if wk == "" {
			continue
		}
		kv.Set([]kvspace.KVPair{
			{keytree.RParam(frameRoot, ret.Name), kvspace.Str(wk)},
			{keytree.WParam(frameRoot, ret.Name), kvspace.Str(wk)},
		})
	}
	if len(params) > 0 {
		kv.Set([]kvspace.KVPair{{keytree.FrameRO(frameRoot), kvspace.Str(strings.Join(params, ","))}})
	}

	return keytree.EntryPC(frameRoot)
}

// HandleReturn 处理 RETURN：清理帧，恢复父 PC。
//
// pc 为 return 指令的绝对路径（如 /vthread/42/[3,0]/[1,0]）。
// 写参已在子帧执行期间经 .wparam 零拷贝直写父帧。
// 嵌套帧 frameRoot 即 callPC，NextPC(frameRoot) 即为父帧下一条指令。
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction) (nextPC, retVal string) {
	vtid := keytree.VtidFromPC(pc)
	vthreadRoot := keytree.VThread(vtid)
	frameRoot := keytree.FrameRoot(pc)

	var returnValue string
	if len(inst.Reads) > 0 {
		v := builtin.ResolveReadValue(kv, frameRoot, inst.Reads[0])
		returnValue = v.Str()
	}

	if frameRoot == vthreadRoot {
		return "", returnValue
	}

	nextPC = op.NextPC(frameRoot)

	kv.UnLink(keytree.Stack(frameRoot))
	kv.DelTree(frameRoot)
	return nextPC, ""
}
// RegisterBlocks 为函数体内所有 BlockStmt label 注册编译后子函数签名。
// 写入 /lib/<pkg>/<name>/<label>，供 br/goto 运行时查找。
func RegisterBlocks(kv kvspace.KVSpace, pkg, parent string, body []ast.Stmt) {
	for _, st := range body {
		if b, ok := st.(*ast.BlockStmt); ok {
			blockKey := keytree.LibFunc(pkg, parent+"/"+b.Label)
			kv.Set([]kvspace.KVPair{{blockKey, kvspace.Str("def "+b.Label+"() -> ()")}})
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
// 成功返回第一条指令的绝对 PC（vthreadRoot/[0,0]）；失败返回 ""。
func Bootstrap(ctx context.Context, kv kvspace.KVSpace, vtid, funcName string, args []string) string {
	pkg, name := "", funcName
	if dot := strings.LastIndex(funcName, keytree.FuncPathSep); dot > 0 {
		pkg = funcName[:dot]
		name = funcName[dot+len(keytree.FuncPathSep):]
	}
	funcKey := keytree.LibFunc(pkg, name)

	vthreadRoot := keytree.VThread(vtid)
	kvspace.MkIndexRecursive(kv, keytree.Stack(vthreadRoot))
	if err := kv.ExtIndex(keytree.Stack(vthreadRoot), funcKey+"/"); err != nil {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: RuntimeError: overlay failed: "+err.Error())
		return ""
	}
	kv.Set([]kvspace.KVPair{
					})

	// 绑定入参（若有）。顶层帧无调用方，值写入 vthreadRoot，.rparam 指向自身槽位。
	if len(args) > 0 {
		sigVal := kvspace.GetOne(kv, funcKey)
		sig := parser.ParseFuncSig(sigVal.Str())
		if err := checkDupParams(sig, funcName); err != "" {
			vthread.SetError(ctx, kv, vtid, "", err)
			return ""
		}
		params := sig.ParamNames()
		pairs := make([]kvspace.KVPair, 0, len(params)*2+1)
		for i, param := range params {
			if i < len(args) {
				dest := vthreadRoot + "/" + param
				pairs = append(pairs,
					kvspace.KVPair{dest, builtin.ResolveReadValue(kv, "", args[i])},
					kvspace.KVPair{keytree.RParam(vthreadRoot, param), kvspace.Str(dest)},
				)
			}
		}
		if len(params) > 0 {
			pairs = append(pairs, kvspace.KVPair{keytree.FrameRO(vthreadRoot), kvspace.Str(strings.Join(params, ","))})
		}
		kv.Set(pairs)
	}

	return keytree.EntryPC(vthreadRoot)
}

// WriteFunc 完成一个函数的全部 KV 写入：
//  0. 先 DelTree 清除旧函数数据（避免旧指令 slot 污染新布局）
//  1. 编译签名写入 /lib/<pkg>.<name>
//  2. 源码副本写入 /lib/<pkg>.<name>.src
//  3. 编译 body 指令写入 /lib/<pkg>.<name>/[i,j]
//  4. 块标签写入 /lib/<pkg>.<name>/<label>/
// frameSlotKey 将槽表达式转换为绝对 KV 路径。
//
//	"x"    → frameRoot + "/x"   (裸标识符)
//	"/abs" → "/abs"             (绝对路径，直通)
//	""     → ""                 (空，忽略)
//	".xxx" → ""                 (引擎保留键，忽略，如 ._ 丢弃槽)
func resolveWritePath(kv kvspace.KVSpace, framePath, name string) string {
	rk := frameSlotKey(framePath, name)
	if r := kvspace.GetOne(kv, keytree.WParam(framePath, name)); !r.IsNil() {
		return r.Str()
	}
	return rk
}

func resolveReadPath(kv kvspace.KVSpace, framePath, name string) string {
	if isLiteral(name) { return "" }
	if r := kvspace.GetOne(kv, keytree.RParam(framePath, name)); !r.IsNil() {
		return r.Str()
	}
	return frameSlotKey(framePath, name)
}

func slotValue(val string, typeMap map[string]string) kvspace.XValue {
	kind := "rwir"
	if t, ok := typeMap[val]; ok {
		kind = t
	} else if isLiteral(val) {
		if val[0] == '"' { kind = "string" } else
		if val == "true" || val == "false" { kind = "bool" } else
		if val[0] >= '0' && val[0] <= '9' || (val[0] == '-' && len(val) > 1) { kind = "int64" }
	}
	return kvspace.Raw(kind, []byte(val))
}

func isLiteral(s string) bool {
	if s == "" { return false }
	return s[0] == '"' || s[0] == '/' || s == "true" || s == "false" ||
		(s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && len(s) > 1)
}

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
	return keytree.Stack(frameRoot) + slot
}

func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	typeMap := lower.InferTypes(fn)
	// 清除旧函数数据：旧指令的读/写槽数量可能多于新函数，
	// 若不清除则旧槽（如 [0,-1]、[0,-2]）会被新执行错误读取。
	kv.DelTree(keytree.LibFunc(pkg, fn.Sig.Name))
	kvspace.MkIndexRecursive(kv, keytree.LibFunc(pkg, fn.Sig.Name)+"/")
	kv.Set([]kvspace.KVPair{
		{keytree.LibSrc(pkg, fn.Sig.Name), kvspace.Str(fn.FullText())},
		{keytree.LibFunc(pkg, fn.Sig.Name), kvspace.Str(fn.Sig.String())},
	})
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body, typeMap)
	RegisterBlocks(kv, pkg, fn.Sig.Name, fn.Body)
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

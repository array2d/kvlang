// Package layoutrwir 将 AST 布局到 KV 空间的执行层。
//
// 存储约定：
//
//	/lib/<pkg>.<name>               编译后签名（XValue kind=rwfunc）
//	/lib/<pkg>.<name>/[i,j]         编译后指令（XValue kind=rwir）
//	/lib/<pkg>.<name>/<label>/      基本块子路径（XValue kind=label）
//	/lib/<pkg>.<name>.src           源码副本（fix-034）
//
// 帧模型：
//
//	callPC = parentFrame/[coord]             调用指令 PC = 子帧根
//	frameRoot/                              extindex → /lib/<pkg>/<name>/（指令查找）
//	frameRoot.returnpc / .callpc             返回地址 / 帧执行进度
//	frameRoot.rparam/<name> /.wparam/<name>  零拷贝读写参重定向
//
// 帧类型由帧根 extindex target 的 XValue.Kind() 区分：rwfunc = 函数帧，label = label 帧。
package layoutrwir

import (
	"context"
	"fmt"
	"strings"

	"kvlang/ast"
	"kvlang/keytree"
	"github.com/array2d/kvspace-go"
	"kvlang/lower"
	"kvlang/rwir"
	"kvlang/rwir/builtin"
	"kvlang/parser"
	"kvlang/vthread"
)

// WriteBody 将 []Stmt 写入 /lib/<pkg>/<name>/ 下的结构化 KV（编译后指令）。
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt, typeMap map[string]string) {
	prefix := keytree.LibFunc(pkg, name)
	idx := 0
	for _, st := range body {
		writeStmt(kv, st, prefix, &idx, typeMap, pkg)
	}
}

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
		if pkg != "" && !builtin.IsNativeOp(opcode) && !rwir.IsControlOp(opcode) &&
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

// HandleCall 执行 CALL：创建子帧，链接指令树，绑定参数，写返回地址。
//
// pc 为调用指令的绝对路径（如 /vthread/42/[3,0]）。callPC 即子帧根。
// 返回被调帧第一条指令 PC（frameRoot/[0,0]）；失败时返回 ""。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir) string {
	vtid := keytree.VtidFromPC(pc)
	funcName := inst.Reads[0].Name

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
	if sigVal.IsNone() {
		vthread.SetError(ctx, kv, vtid, pc, "NameError: func signature not found: "+funcName)
		return ""
	}
	funcSig := parser.ParseFuncSig(string(sigVal.RawBytes()))

	if err := checkDupParams(funcSig, funcName); err != "" {
		vthread.SetError(ctx, kv, vtid, pc, err)
		return ""
	}

	callerFrameRoot := keytree.FrameRoot(pc)
	frameRoot := pc // callPC = 子帧根

	kvspace.MkIndexRecursive(kv, keytree.Stack(frameRoot))
	if err := kv.ExtIndex(keytree.Stack(frameRoot), funcKey+"/"); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "RuntimeError: overlay failed: "+err.Error())
		return ""
	}

	// 系统变量
	kv.Set([]kvspace.KVPair{
		{keytree.ReturnPC(frameRoot), kvspace.String(rwir.NextPC(pc))},
		{keytree.CallPC(frameRoot), kvspace.String(keytree.EntryPC(frameRoot))},
		{frameRoot + keytree.MemberSep + keytree.SegRParam + "/", kvspace.Raw(kvspace.KindIndex, nil)},
		{frameRoot + keytree.MemberSep + keytree.SegWParam + "/", kvspace.Raw(kvspace.KindIndex, nil)},
	})

	// 读参零拷贝
	litSeq := 0
	params := funcSig.ParamNames()
	var paramPairs []kvspace.KVPair
	for i, param := range params {
		if i+1 < len(inst.Reads) {
			arg := inst.Reads[i+1].Name
			rk := resolveReadPath(kv, callerFrameRoot, arg)
			if rk == "" && isLiteral(arg) {
				rk = fmt.Sprintf("%s/._lit%d", callerFrameRoot, litSeq)
				paramPairs = append(paramPairs, kvspace.KVPair{rk, builtin.ResolveReadValue(kv, callerFrameRoot, arg)})
				litSeq++
			}
			if rk != "" {
				paramPairs = append(paramPairs, kvspace.KVPair{keytree.RParam(frameRoot, param), kvspace.String(rk)})
			}
		}
	}
	if len(paramPairs) > 0 {
		kv.Set(paramPairs)
	}

	// 写参零拷贝
	callerLink := keytree.Stack(callerFrameRoot)
	addr0 := rwir.ExtractAddr0FromPC(pc)
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
			{keytree.RParam(frameRoot, ret.Name), kvspace.String(wk)},
			{keytree.WParam(frameRoot, ret.Name), kvspace.String(wk)},
		})
	}
	if len(params) > 0 {
		kv.Set([]kvspace.KVPair{{keytree.FrameRO(frameRoot), kvspace.String(strings.Join(params, ","))}})
	}

	return keytree.EntryPC(frameRoot)
}

// HandleReturn 处理 RETURN：读 .returnpc，清理帧。
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir) (nextPC, retVal string) {
	vtid := keytree.VtidFromPC(pc)
	vthreadRoot := keytree.VThread(vtid)
	frameRoot := keytree.FrameRoot(pc)

	if len(inst.Reads) > 0 {
		panic("return 不得带参数，返回值通过写参零拷贝传递")
	}

	if frameRoot == vthreadRoot {
		return "", ""
	}

	nextPC = kvspace.GetOne(kv, keytree.ReturnPC(frameRoot)).Str()

	kv.UnLink(keytree.Stack(frameRoot))
	kv.DelTree(frameRoot)
	return nextPC, ""
}

// HandleLabel 创建或复用 label 帧。
//   - 新 label：在当前帧下创建子帧（嵌套），写 .returnpc .callpc
//   - TCO：若祖先链中存在同名 label，丢弃中间帧，跳回目标入口
func HandleLabel(ctx context.Context, kv kvspace.KVSpace, pc, labelFullPath string) string {
	vtid := keytree.VtidFromPC(pc)

	// 解析 /lib/ 路径 → pkg + labelPath
	var pkg, labelPath string
	libPrefix := keytree.LibRoot + keytree.PathSegSep
	if strings.HasPrefix(labelFullPath, libPrefix) {
		rest := labelFullPath[len(libPrefix):]
		if dot := strings.LastIndex(rest, keytree.FuncPathSep); dot > 0 {
			pkg = rest[:dot]
			labelPath = rest[dot+len(keytree.FuncPathSep):]
		} else {
			labelPath = rest
		}
	} else if dot := strings.LastIndex(labelFullPath, keytree.FuncPathSep); dot > 0 {
		pkg = labelFullPath[:dot]
		labelPath = labelFullPath[dot+len(keytree.FuncPathSep):]
	} else {
		labelPath = labelFullPath
	}

	// 取路径末段为 label 名
	labelName := labelPath
	if slash := strings.LastIndex(labelPath, keytree.PathSegSep); slash >= 0 {
		labelName = labelPath[slash+len(keytree.PathSegSep):]
	}

	currentFrame := keytree.FrameRoot(pc)

	// ── TCO：仅在 label 帧内搜索祖先链（不跨越 rwfunc 边界）──
	if extKind(kv, currentFrame) == kvspace.KindLabel {
		for f := currentFrame; f != ""; f = keytree.ParentFrame(f) {
			if extKind(kv, f) == kvspace.KindRwfunc {
				break
			}
			trimmed := strings.TrimRight(f, keytree.PathSegSep)
			lastSeg := trimmed[strings.LastIndex(trimmed, keytree.PathSegSep)+len(keytree.PathSegSep):]
			if lastSeg == labelName {
				// 找到目标 → 丢弃目标子帧，跳回入口
				for d := currentFrame; d != f; d = keytree.ParentFrame(d) {
					kv.UnLink(keytree.Stack(d))
					kv.DelTree(d)
				}
				kv.Set([]kvspace.KVPair{
					{keytree.CallPC(f), kvspace.String(keytree.EntryPC(f))},
				})
				return keytree.EntryPC(f)
			}
		}
	}

	// ── 新建 label 帧（当前帧下嵌套）─────────────────────────
	labelFrame := currentFrame + "/" + labelName + "/"
	kv.DelTree(labelFrame)
	kvspace.MkIndexRecursive(kv, labelFrame)

	labelKey := keytree.LibFunc(pkg, labelPath)
	if err := kv.ExtIndex(keytree.Stack(labelFrame), labelKey+"/"); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "RuntimeError: label overlay failed: "+err.Error())
		return ""
	}

	kv.Set([]kvspace.KVPair{
		{keytree.ReturnPC(labelFrame), kvspace.String(rwir.NextPC(pc))},
		{keytree.CallPC(labelFrame), kvspace.String(keytree.EntryPC(labelFrame))},
	})
	return keytree.EntryPC(labelFrame)
}

// RegisterBlocks 为函数体内所有 BlockStmt label 注册 label 签名（XValue kind=label）。
func RegisterBlocks(kv kvspace.KVSpace, pkg, parent string, body []ast.Stmt) {
	for _, st := range body {
		if b, ok := st.(*ast.BlockStmt); ok {
			blockKey := keytree.LibFunc(pkg, parent+"/"+b.Label)
			kv.Set([]kvspace.KVPair{{blockKey, kvspace.Raw(kvspace.KindLabel, nil)}})
			RegisterBlocks(kv, pkg, parent+"/"+b.Label, b.Body)
		}
	}
}

// Bootstrap 为 vthread 的顶层入口函数建立虚线程根帧。
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

	// 虚线程根也是函数帧，写 .callpc
	kv.Set([]kvspace.KVPair{
		{keytree.CallPC(vthreadRoot), kvspace.String(keytree.EntryPC(vthreadRoot))},
	})

	if len(args) > 0 {
		sigVal := kvspace.GetOne(kv, funcKey)
		sig := parser.ParseFuncSig(string(sigVal.RawBytes()))
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
					kvspace.KVPair{keytree.RParam(vthreadRoot, param), kvspace.String(dest)},
				)
			}
		}
		if len(params) > 0 {
			pairs = append(pairs, kvspace.KVPair{keytree.FrameRO(vthreadRoot), kvspace.String(strings.Join(params, ","))})
		}
		kv.Set(pairs)
	}

	return keytree.EntryPC(vthreadRoot)
}

// WriteFunc 写函数到 /lib/：签名（kind=rwfunc）、源码、指令体、label 块。
// countDirectInsts 返回 body 中直接 rwir 指令数量（不计 scope/block 内）。
func countDirectInsts(body []ast.Stmt) int32 {
	var n int32
	for _, st := range body {
		if _, ok := st.(*ast.Instruction); ok {
			n++
		}
	}
	return n
}

// WriteRwir 将 rwir 声明写入 /lib/<pkg>/<name>，kind="rwir"，无指令体。
func WriteRwir(kv kvspace.KVSpace, pkg string, decl *ast.RwirDecl) {
	kv.DelTree(keytree.LibFunc(pkg, decl.Sig.Name))
	kv.Set([]kvspace.KVPair{
		{keytree.LibFunc(pkg, decl.Sig.Name), kvspace.Rwir(decl.Sig.NumReads(), decl.Sig.NumWrites(), []byte(decl.SigString()))},
	})
}

func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	typeMap := lower.InferTypes(fn)
	kv.DelTree(keytree.LibFunc(pkg, fn.Sig.Name))
	kvspace.MkIndexRecursive(kv, keytree.LibFunc(pkg, fn.Sig.Name)+"/")
	kv.Set([]kvspace.KVPair{
		{keytree.LibSrc(pkg, fn.Sig.Name), kvspace.String(fn.FullText())},
		{keytree.LibFunc(pkg, fn.Sig.Name), kvspace.Rwfunc(countDirectInsts(fn.Body), fn.Sig.NumReads(), fn.Sig.NumWrites(), []byte(fn.Sig.String()))},
	})
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body, typeMap)
	RegisterBlocks(kv, pkg, fn.Sig.Name, fn.Body)
}

// ── 辅助函数 ────────────────────────────────────────────────────────────────

func resolveWritePath(kv kvspace.KVSpace, framePath, name string) string {
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if r := kvspace.GetOne(kv, keytree.WParam(f, name)); !r.IsNone() {
			return r.Str()
		}
		if extKind(kv, f) == kvspace.KindRwfunc {
			break
		}
	}
	return frameSlotKey(framePath, name)
}

func resolveReadPath(kv kvspace.KVSpace, framePath, name string) string {
	if isLiteral(name) { return "" }
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if r := kvspace.GetOne(kv, keytree.RParam(f, name)); !r.IsNone() {
			return r.Str()
		}
		if extKind(kv, f) == kvspace.KindRwfunc {
			break
		}
	}
	// label 帧变量逃逸到函数帧，查找对齐 resolveWriteKey
	funcFrame := framePath
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if extKind(kv, f) == kvspace.KindRwfunc {
			funcFrame = f
			break
		}
	}
	if v := kvspace.GetOne(kv, keytree.Stack(funcFrame)+name); !v.IsNone() {
		return keytree.Stack(funcFrame) + name
	}
	return frameSlotKey(framePath, name)
}

func slotValue(val string, typeMap map[string]string) kvspace.XValue {
	kind := kvspace.KindRwir
	if t, ok := typeMap[val]; ok {
		kind = t
	} else if isLiteral(val) {
		if val[0] == '"' { kind = kvspace.KindString } else if val == "true" || val == "false" { kind = kvspace.KindBool } else if val[0] >= '0' && val[0] <= '9' || (val[0] == '-' && len(val) > 1) { if strings.Contains(val, ".") || strings.ContainsAny(val, "eE") { kind = kvspace.KindFloat64 } else { kind = kvspace.KindInt64 } }
	}
	return kvspace.Raw(kind, []byte(val))
}

func isLiteral(s string) bool {
	if s == "" { return false }
	return s[0] == '"' || s[0] == '/' || s == "true" || s == "false" || s == "null" ||
		(s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && len(s) > 1)
}

func frameSlotKey(frameRoot, slot string) string {
	if slot == "" { return "" }
	if slot[0] == '/' { return slot }
	if slot[0] == '.' { return "" }
	return keytree.Stack(frameRoot) + slot
}

// extKind 读帧根 extindex target 的 XValue.Kind()：rwfunc=函数帧，label=label 帧。
func extKind(kv kvspace.KVSpace, frameRoot string) string {
	trimmed := strings.TrimRight(frameRoot, keytree.PathSegSep)
	if trimmed == "" || trimmed == keytree.PathSegSep {
		return ""
	}
	parent, dirName := kvspace.SepPath(trimmed)
	if parent != keytree.PathSegSep {
		parent += kvspace.DirIndexSuf
	}
	extVal := kv.Get(parent, []string{dirName + kvspace.DirIndexSuf})[0]
	_, extTarget := kvspace.DecodeExtIndex(extVal)
	if extTarget == "" {
		return ""
	}
	return kvspace.GetOne(kv, strings.TrimRight(extTarget, keytree.PathSegSep)).Kind()
}

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

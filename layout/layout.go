// Package layoutrwir 将 AST 布局到 KV 空间的执行层。
//
// 存储约定：
//
//	/lib/<pkg>.<name>/[0,0]         编译后签名（XValue kind=rwfunc, body=[nr|nw]）
//	/lib/<pkg>.<name>/<param>       命名参数→slot 指针（XValue kind=string, isptr=1）
//	/lib/<pkg>.<name>/[i,j]         编译后指令（XValue kind=rwir），i 从 1 开始
//	/lib/<pkg>.<name>/<label>/      基本块子路径（XValue kind=label）
//	/lib/<pkg>.<name>.src           源码副本（fix-034）
//
// 帧模型（指针传址方案）：
//
//	callPC = parentFrame/[coord]             调用指令 PC = 子帧根
//	frameRoot/                              extindex → /lib/<pkg>/<name>/（指令+参数声明）
//	frameRoot/<name> → ext→ Ptr("[0,-j]")    命名参数 → slot 映射（layout 写入，只读）
//	frameRoot/[0,-j] → Char(path)            runtime 实参槽（HandleCall 写入，无 ext 冲突）
//	frameRoot.returnpc / .callpc             返回地址 / 帧执行进度
//	frameRoot‥lib                            帧类型标记
//
// 帧类型由 .lib 标记识别：存在 → rwfunc 帧。
package layout

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	"kvlang/ast"
	"kvlang/keytree"
	"kvlang/symbol"
	"github.com/array2d/kvspace-go"
	"kvlang/lower"
	"kvlang/rwir"
	"kvlang/rwir/builtin"
	"kvlang/vthread"
)

// WriteBody 将 []Stmt 写入 /lib/<pkg>/<name>/ 下的结构化 KV（编译后指令）。
// offset: 起始 idx 偏移（顶层函数=1，[0,...] 被函数定义占用）。
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt, typeMap map[string]string, offset int) {
	prefix := keytree.LibFunc(pkg, name)
	idx := offset
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
		if pkg != "" && !builtin.IsNativeRwir(opcode) && !builtin.IsGlobalRwir(opcode) && !rwir.IsControlOp(opcode) &&
			!strings.Contains(opcode, keytree.MemberSep) && !strings.HasPrefix(opcode, keytree.LibRoot+keytree.PathSegSep) &&
			symbol.Lookup(opcode).Word != "assign" {
			opcode = pkg + keytree.MemberSep + opcode
		}
		pairs := make([]kvspace.KVPair, 0, 1+len(reads)+len(s.Writes))
		if opcode != "" {
			pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s/[%d,0]", prefix, n), Val: slotValue(opcode, typeMap)})
		}
		for j, r := range reads {
			pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s/[%d,-%d]", prefix, n, j+1), Val: slotValue(r, typeMap)})
		}
		for j, w := range s.Writes {
			pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s/[%d,%d]", prefix, n, j+1), Val: slotValue(w, typeMap)})
		}
		if len(pairs) > 0 {
			kv.Set(pairs)
		}
		*idx = n + 1
	case *ast.ScopeStmt:
		// scope 指令 flat key: /lib/func/scopeName[coord]
		scopePrefix := prefix + "/" + s.Label
		scopeIdx := 0
		for _, child := range s.Body {
			writeStmtScope(kv, child, scopePrefix, &scopeIdx, typeMap, pkg, prefix)
		}

	}
}

// writeStmtScope 写 scope 体内指令，key 格式 funcPrefix/scopeName[coord]。
// funcPrefix 为 /lib/<func>，所有 scope（含嵌套）均平级使用 funcPrefix。
func writeStmtScope(kv kvspace.KVSpace, st ast.Stmt, scopePrefix string, idx *int, typeMap map[string]string, pkg string, funcPrefix string) {
	switch s := st.(type) {
	case *ast.Instruction:
		n := *idx
		for j, w := range s.Writes {
			if j < len(s.WriteTypes) && s.WriteTypes[j] != "" {
				typeMap[w] = s.WriteTypes[j]
			}
		}
		opcode, reads := s.Flat()
		if pkg != "" && !builtin.IsNativeRwir(opcode) && !builtin.IsGlobalRwir(opcode) && !rwir.IsControlOp(opcode) &&
			!strings.Contains(opcode, keytree.MemberSep) && !strings.HasPrefix(opcode, keytree.LibRoot+keytree.PathSegSep) &&
			symbol.Lookup(opcode).Word != "assign" {
			opcode = pkg + keytree.MemberSep + opcode
		}
		pairs := make([]kvspace.KVPair, 0, 1+len(reads)+len(s.Writes))
		if opcode != "" {
			pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s[%d,0]", scopePrefix, n), Val: slotValue(opcode, typeMap)})
		}
		for j, r := range reads {
			pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s[%d,-%d]", scopePrefix, n, j+1), Val: slotValue(r, typeMap)})
		}
		for j, w := range s.Writes {
			pairs = append(pairs, kvspace.KVPair{Key: fmt.Sprintf("%s[%d,%d]", scopePrefix, n, j+1), Val: slotValue(w, typeMap)})
		}
		if len(pairs) > 0 {
			kv.Set(pairs)
		}
		*idx = n + 1
	case *ast.ScopeStmt:
		// 嵌套 scope 也用 funcPrefix，保持平级
		childPrefix := funcPrefix + "/" + s.Label
		childIdx := 0
		for _, child := range s.Body {
			writeStmtScope(kv, child, childPrefix, &childIdx, typeMap, pkg, funcPrefix)
		}
	}
}

// HandleCall 执行 CALL：创建子帧，链接指令树，绑定参数，写返回地址。
//
// pc 为调用指令的绝对路径（如 /vthread/42/[3,0]）。callPC 即子帧根。
// 返回被调帧第一条指令 PC（frameRoot/[0,0]）；失败时返回 ""。
// HandleCall 处理函数调用：创建子帧，建立 extindex，写入实参到 &[0,-j] slot。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir) string {
	vtid := keytree.VtidFromPC(pc)
	funcName := inst.Reads[0].Name

	var pkg string
	libPrefix := keytree.LibRoot + keytree.PathSegSep
	if strings.HasPrefix(funcName, libPrefix) {
		rest := funcName[len(libPrefix):]
		if dot := strings.LastIndex(rest, keytree.MemberSep); dot > 0 {
			pkg = rest[:dot]
			funcName = rest[dot+len(keytree.MemberSep):]
		} else {
			funcName = rest
		}
	} else if dot := strings.LastIndex(funcName, keytree.MemberSep); dot > 0 {
		pkg = funcName[:dot]
		funcName = funcName[dot+len(keytree.MemberSep):]
	}
	funcKey := keytree.LibFunc(pkg, funcName)
	funcDir := funcKey + keytree.PathSegSep

	// 读函数签名
	sigVal := kvspace.GetOne(kv, funcDir+"[0,0]")
	if kvspace.IsNone(sigVal) || sigVal.Kind() != kvspace.KindRwfunc {
		vthread.SetError(ctx, kv, vtid, pc, "NameError: rwir/rwfunc not found: "+funcName)
		return ""
	}
	rwfunc := sigVal.(kvspace.Rwfunc)
	nr, nw := int(rwfunc.NumReads()), int(rwfunc.NumWrites())

	// 列目录提取命名参数 → slot 映射
	children := kv.List(funcDir, false, false)
	nameSlot := map[string]string{} // "a" → "[0,-1]"
	for _, child := range children {
		if len(child) == 0 || child[0] == '[' {
			continue
		}
		v := kvspace.GetOne(kv, funcDir+child)
		if kvspace.IsPtr(v) {
			nameSlot[child] = kvspace.PtrTarget(v)
		}
	}

	callerFrameRoot := keytree.FrameRoot(pc)
	frameRoot := pc

	kvspace.MkIndexRecursive(kv, keytree.Stack(frameRoot))
	if err := kv.ExtIndex(keytree.Stack(frameRoot), funcDir); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "RuntimeError: overlay failed: "+err.Error())
		return ""
	}

	// 系统变量
	kv.Set([]kvspace.KVPair{
		{Key: keytree.ReturnPC(frameRoot), Val: kvspace.NewCharByte([]byte(rwir.NextPC(pc))...)},
		{Key: keytree.CallPC(frameRoot), Val: kvspace.NewCharByte([]byte(keytree.EntryPC(frameRoot))...)},
		{Key: keytree.Stack(frameRoot) + keytree.SegLib, Val: kvspace.NewCharByte([]byte(funcKey)...)},
	})

	// 读参：写 caller arg 地址到 frameRoot/[0,-j]
	litSeq := 0
	var argPairs []kvspace.KVPair
	paramTypes := rwfunc.ParamTypes()
	for i := 0; i < nr; i++ {
		slot := fmt.Sprintf("%s[0,-%d]", frameRoot+keytree.PathSegSep, i+1)
		if i+1 < len(inst.Reads) {
			arg := inst.Reads[i+1]
			rk := resolveReadPath(kv, callerFrameRoot, arg.Name)
			// 类型检查：参数定义的数组性与实参的数组性必须一致
			if i < len(paramTypes) && paramTypes[i] != "" {
				argVal := arg.Val
				if !isConcreteVal(argVal) && rk != "" {
					argVal = kvspace.GetOne(kv, rk)
				}
				if !kvspace.IsNone(argVal) && isArrayType(paramTypes[i]) != isArrayArg(argVal) {
					vthread.SetError(ctx, kv, vtid, pc, fmt.Sprintf(
						"TypeError: %s param %q declared %s but got %s",
						funcName, arg.Name, paramTypes[i], arrayDesc(argVal)))
					return ""
				}
			}
			if isConcreteVal(arg.Val) {
				if rk == "" {
					rk = fmt.Sprintf("%s/._lit%d", callerFrameRoot, litSeq)
					litSeq++
				}
				argPairs = append(argPairs, kvspace.KVPair{Key: rk, Val: arg.Val})
			}
			if rk != "" {
				argPairs = append(argPairs, kvspace.KVPair{Key: slot, Val: kvspace.NewCharByte([]byte(rk)...)})
			}
		}
	}
	// 写参（返回值）：写 caller 写槽地址到 frameRoot/[0,+j]
	for i := 0; i < nw; i++ {
		slot := fmt.Sprintf("%s[0,%d]", frameRoot+keytree.PathSegSep, i+1)
		if i < len(inst.Writes) {
			wk := resolveReadPath(kv, callerFrameRoot, inst.Writes[i].Name)
			if wk != "" {
				argPairs = append(argPairs, kvspace.KVPair{Key: slot, Val: kvspace.NewCharByte([]byte(wk)...)})
			}
		}
	}
	if len(argPairs) > 0 {
		kv.Set(argPairs)
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

	nextPC = kvspace.GetOne(kv, keytree.ReturnPC(frameRoot)).ValueString()

	kv.DelExtIndex(keytree.Stack(frameRoot))
	kv.DelTree(frameRoot)
	return nextPC, ""
}

// HandleLabel 创建或复用 label 帧。
//   - 新 label：在当前帧下创建子帧（嵌套），写 .returnpc .callpc
//   - TCO：若祖先链中存在同名 label，丢弃中间帧，跳回目标入口

// RegisterBlocks 为函数体内所有 BlockStmt label 注册 label 签名（XValue kind=label）。

// Bootstrap 为 vthread 的顶层入口函数建立虚线程根帧。
func Bootstrap(ctx context.Context, kv kvspace.KVSpace, vtid, funcName string, args []string) string {
	pkg, name := "", funcName
	if dot := strings.LastIndex(funcName, keytree.MemberSep); dot > 0 {
		pkg = funcName[:dot]
		name = funcName[dot+len(keytree.MemberSep):]
	}
	funcKey := keytree.LibFunc(pkg, name)
	funcDir := funcKey + keytree.PathSegSep

	// 读函数签名
	sigVal := kvspace.GetOne(kv, funcDir+"[0,0]")
	if kvspace.IsNone(sigVal) || sigVal.Kind() != kvspace.KindRwfunc {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: rwir/rwfunc not found: "+funcName)
		return ""
	}
	rwfunc := sigVal.(kvspace.Rwfunc)
	nr := int(rwfunc.NumReads())

	vthreadRoot := keytree.VThread(vtid)
	kvspace.MkIndexRecursive(kv, keytree.Stack(vthreadRoot))
	if err := kv.ExtIndex(keytree.Stack(vthreadRoot), funcDir); err != nil {
		vthread.SetError(ctx, kv, vtid, "", "Bootstrap: RuntimeError: overlay failed: "+err.Error())
		return ""
	}

	kv.Set([]kvspace.KVPair{
		{Key: keytree.CallPC(vthreadRoot), Val: kvspace.NewCharByte([]byte(keytree.EntryPC(vthreadRoot))...)},
		{Key: keytree.Stack(vthreadRoot) + keytree.SegLib, Val: kvspace.NewCharByte([]byte(funcKey)...)},
	})

	if len(args) > 0 {
		pairs := make([]kvspace.KVPair, 0, nr)
		for i := 0; i < nr && i < len(args); i++ {
			slot := fmt.Sprintf("%s[0,-%d]", vthreadRoot+keytree.PathSegSep, i+1)
			pairs = append(pairs,
				kvspace.KVPair{Key: slot, Val: builtin.ResolveReadValue(kv, "", rwir.Param{Name: args[i]})},
			)
		}
		if len(pairs) > 0 {
			kv.Set(pairs)
		}
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

// WriteFunc 写函数到 /lib/ 下。
//
// 布局：
//
//	/lib/<pkg>.<name>/[0,0]     → Rwfunc(nr, nw, al=numInsts)   ← 函数签名
//	/lib/<pkg>.<name>/<param>   → Ptr(char, "[0,-j]", 1)         ← 命名参数→slot
//	/lib/<pkg>.<name>/<ret>     → Ptr(char, "[0,+j]", 1)         ← 命名返回值→slot
//	/lib/<pkg>.<name>/[1,0]     → instruction 0 opcode            ← idx+1
//	...
func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	typeMap := lower.InferTypes(fn)
	funcDir := keytree.LibFunc(pkg, fn.Sig.Name)
	kv.DelTree(funcDir)
	kvspace.MkIndexRecursive(kv, funcDir+"/")

	nr, nw := fn.Sig.NumReads(), fn.Sig.NumWrites()
	paramTypes := make([]string, len(fn.Sig.Params))
	for i, p := range fn.Sig.Params {
		paramTypes[i] = p.Type
	}
	pairs := []kvspace.KVPair{
		{Key: funcDir + "/[0,0]", Val: kvspace.NewRwfuncWithTypes(countDirectInsts(fn.Body), nr, nw, paramTypes)},
		{Key: keytree.LibSrc(pkg, fn.Sig.Name), Val: kvspace.NewCharByte([]byte(fn.FullText())...)},
	}

	for i, p := range fn.Sig.Params {
		slot := fmt.Sprintf("[0,-%d]", i+1)
		pairs = append(pairs, kvspace.KVPair{Key: funcDir + "/" + p.Name, Val: kvspace.NewPtr(kvspace.KindCharByte, slot, 1)})
	}
	for i, r := range fn.Sig.Returns {
		slot := fmt.Sprintf("[0,%d]", i+1)
		pairs = append(pairs, kvspace.KVPair{Key: funcDir + "/" + r.Name, Val: kvspace.NewPtr(kvspace.KindCharByte, slot, 1)})
	}
	kv.Set(pairs)
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body, typeMap, 1) // 指令从 [1,0] 开始
}

// WriteRwirDecl 将用户声明的 rwir（读写码，无体）写入 /sys/rwir/<opcode>，kind=rwir。
func WriteRwirDecl(kv kvspace.KVSpace, decl *ast.RwirDecl) {
	opcode := decl.Sig.Name
	if decl.Pkg != "" {
		opcode = decl.Pkg + keytree.MemberSep + opcode
	}
	kv.Set([]kvspace.KVPair{{
		Key: keytree.SysRwir(opcode),
		Val: kvspace.NewRwir(decl.Sig.NumReads(), decl.Sig.NumWrites(), decl.SigString()),
	}})
}

// ── 辅助函数 ────────────────────────────────────────────────────────────────

func resolveReadPath(kv kvspace.KVSpace, framePath, name string) string {
	if isLiteral(name) { return "" }
	funcFrame := framePath
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if !kvspace.IsNone(kvspace.GetOne(kv, keytree.Stack(f)+keytree.SegLib)) {
			funcFrame = f
			break
		}
	}
	v := kvspace.GetOne(kv, keytree.Stack(funcFrame)+name)
	if kvspace.IsNone(v) {
		return frameSlotKey(funcFrame, name)
	}
	// 命名 param → Ptr → slot → 沿 Char 链到底（HandleCall 写入时 Char(path) 的最终目标）
	if kvspace.IsPtr(v) {
		path := keytree.Stack(funcFrame) + kvspace.PtrTarget(v)
		for {
			nextVal := kvspace.GetOne(kv, path)
			if kvspace.IsNone(nextVal) || nextVal.Kind() != kvspace.KindCharByte {
				return path
			}
			path = nextVal.ValueString()
		}
	}
	return keytree.Stack(funcFrame) + name
}

func slotValue(val string, typeMap map[string]string) kvspace.XValue {
	if !isLiteral(val) {
		return kvspace.NewRwir(0, 0, val)
	}
	kind := kvspace.KindRwir
	if val[0] == '"' {
		kind = kvspace.KindCharByte
	} else if val == "true" || val == "false" {
		kind = kvspace.KindBool
	} else if val[0] >= '0' && val[0] <= '9' || (val[0] == '-' && len(val) > 1) {
		if strings.Contains(val, ".") || strings.ContainsAny(val, "eE") { kind = kvspace.KindFloat64 } else { kind = kvspace.KindInt64 }
	}
	switch kind {
	case kvspace.KindCharByte:
		s := val
		if len(s) > 0 && s[0] == '"' { s = s[1:] }
		return kvspace.NewCharByte([]byte(s)...)
	case kvspace.KindBool:
		return kvspace.NewBool(val == "true")
	case kvspace.KindInt64:
		if v, ok := builtin.TryParseNumber(val); ok {
			return v
		}
		return kvspace.NewInt64(0)
	case kvspace.KindFloat64:
		f, _ := strconv.ParseFloat(val, 64)
		return kvspace.NewFloat64(f)
	default:
		return kvspace.NewRwir(0, 0, val)
	}
}

// isConcreteVal 检查 Val 是否为具体值类型（非 rwir/rwfunc）。
func isConcreteVal(v kvspace.XValue) bool {
	if kvspace.IsNone(v) { return false }
	return v.Kind() != kvspace.KindRwir && v.Kind() != kvspace.KindRwfunc
}

// isArrayType 判断 kindexp 是否含数组修饰符（[] [N]）。
func isArrayType(t string) bool {
	return strings.Contains(t, "[")
}

// isArrayArg 判断实参是否为数组。charbyte 是标量（字符串），即使 ArrayLen>1（多字节）。
func isArrayArg(v kvspace.XValue) bool {
	if kvspace.IsNone(v) { return false }
	return v.Kind() != kvspace.KindCharByte && v.ArrayLen() > 1
}

// arrayDesc 描述值的数组性（scalar/array/None）。
func arrayDesc(v kvspace.XValue) string {
	if kvspace.IsNone(v) { return "None" }
	if isArrayArg(v) { return "array" }
	return "scalar"
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

// ExtKind 判断帧类型：有 .lib → rwfunc；否则 → scope 或空。
func ExtKind(kv kvspace.KVSpace, frameRoot string) string {
	if !kvspace.IsNone(kvspace.GetOne(kv, keytree.Stack(frameRoot)+keytree.SegLib)) {
		return kvspace.KindRwfunc
	}
	return ""
}

// rwfuncFrameRoot 从 framePath 向上找到最近的 rwfunc 帧根（通过 .lib 标记识别）。
func rwfuncFrameRoot(kv kvspace.KVSpace, framePath string) string {
	for f := framePath; f != ""; f = keytree.ParentFrame(f) {
		if !kvspace.IsNone(kvspace.GetOne(kv, keytree.Stack(f)+keytree.SegLib)) {
			return f
		}
	}
	return framePath
}

// HandleScope goto/br 跳入 scope。scope 帧为 rwfunc 平级子帧，不建 extindex。
func HandleScope(ctx context.Context, kv kvspace.KVSpace, pc, scopeName string) string {
	rwRoot := strings.TrimRight(rwfuncFrameRoot(kv, keytree.FrameRoot(pc)), keytree.PathSegSep)
	scopeFrame := rwRoot + keytree.PathSegSep + scopeName + keytree.PathSegSep

	callpcKey := keytree.CallPC(scopeFrame)
	exists := !kvspace.IsNone(kvspace.GetOne(kv, callpcKey))

	if !exists {
		kvspace.MkIndexRecursive(kv, scopeFrame)
		kv.Set([]kvspace.KVPair{
			{Key: keytree.ReturnPC(scopeFrame), Val: kvspace.NewCharByte([]byte(rwir.NextPC(pc))...)},
		})
	}
	// callpc 每次更新，returnpc 仅首次设置（保持原始返回路径）
	kv.Set([]kvspace.KVPair{
		{Key: keytree.CallPC(scopeFrame), Val: kvspace.NewCharByte([]byte(keytree.ScopeEntryPC(scopeFrame))...)},
	})
	return keytree.ScopeEntryPC(scopeFrame)
}

// HandleScopeReturn scope 帧隐式 return：读 .returnpc，DelTree 自身。
func HandleScopeReturn(ctx context.Context, kv kvspace.KVSpace, pc string) string {
	frameRoot := keytree.FrameRoot(pc)
	parentPC := kvspace.GetOne(kv, keytree.ReturnPC(frameRoot)).ValueString()
	kv.DelExtIndex(keytree.Stack(frameRoot))
	kv.DelTree(frameRoot)
	return parentPC
}


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
// 帧模型（callPC 即子帧根）：
//
//	callPC = parentFrame/[coord]              调用指令 PC = 子帧根
//	frameRoot/.funclib                         软链接 → /lib/<pkg>/<name>（只读指令）→ keytree.FuncLib(frameRoot)
//	frameRoot/<param>                           参数（本帧局部变量）
//	frameRoot/.rootfunc                         入口函数名（keytree.RootFunc），TCO 不更新
//	frameRoot/.rparam/<name> /.wparam/<name>   零拷贝读写参重定向
//
// kvcpu 取指：linkBase = FuncLib(FrameRoot(pc))，coord 从 pc 末尾提取
package layoutrwir

import (
	"context"
	"fmt"
	"strconv"
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
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt, typeMap map[string]string) {
	prefix := keytree.LibFunc(pkg, name)
	idx := 0
	for _, st := range body {
		writeStmt(kv, st, prefix, &idx, typeMap)
	}
}

// writeStmt 将单条 Stmt 写入 KV 空间。
// lower.File 保证调用时只剩 *Instruction 和 *BlockStmt，其余类型无操作。
func writeStmt(kv kvspace.KVSpace, st ast.Stmt, prefix string, idx *int, typeMap map[string]string) {
	switch s := st.(type) {
	case *ast.Instruction:
		n := *idx
		opcode, reads := s.Flat()
		if opcode != "" {
			kv.Set(fmt.Sprintf("%s/[%d,0]", prefix, n), slotValue(opcode, typeMap))
		}
		for j, r := range reads {
			kv.Set(fmt.Sprintf("%s/[%d,-%d]", prefix, n, j+1), slotValue(r, typeMap))
		}
		for j, w := range s.Writes {
			kv.Set(fmt.Sprintf("%s/[%d,%d]", prefix, n, j+1), slotValue(w, typeMap))
		}
		*idx = n + 1
	case *ast.BlockStmt:
		sub := prefix + "/" + s.Label
		i := 0
		for _, child := range s.Body {
			writeStmt(kv, child, sub, &i, typeMap)
		}
	}
}

// HandleCall 执行 CALL：链接函数指令树，绑定参数。
//
// pc 为调用指令的绝对路径（如 /vthread/42/[3,0]）。callPC 即子帧根。
// tail=true 时执行 TCO：复用当前帧，仅重链 .funclib。
// 返回被调帧第一条指令 PC（frameRoot/[0,0]）；失败时返回 ""。
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

	// TCO：复用当前帧，仅重链 .funclib 到目标块（.rootfunc 不更新）
	if tail {
		frameRoot := keytree.FrameRoot(pc)
		kv.Unlink(keytree.FuncLib(frameRoot))
		if err := kv.Link(funcKey, keytree.FuncLib(frameRoot)); err != nil {
			vthread.SetError(ctx, kv, vtid, pc, "tco RuntimeError: link failed: "+err.Error())
			return ""
		}
		return keytree.EntryPC(frameRoot)
	}

	// 普通 call：callPC 即子帧根
	callerFrameRoot := keytree.FrameRoot(pc)
	frameRoot := keytree.ChildFrameRoot(pc)

	// 链接只读指令区：frameRoot/.funclib → /lib/<pkg>/<name>
	if err := kv.Link(funcKey, keytree.FuncLib(frameRoot)); err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "RuntimeError: link failed: "+err.Error())
		return ""
	}

	kv.Set(keytree.RootFunc(frameRoot), kvspace.Str(funcName))

	// 读参零拷贝：.rparam 重定向到调用方值位置
	// 字面量存为临时变量；变量引用追踪 .rparam 链到源头
	litSeq := 0
	params := funcSig.ParamNames()
	for i, param := range params {
		if i+1 < len(inst.Reads) {
			arg := inst.Reads[i+1]
			rk := resolveReadPath(kv, callerFrameRoot, arg)
			if rk == "" && isLiteral(arg) {
				rk = fmt.Sprintf("%s/_lit%d", callerFrameRoot, litSeq)
				kv.Set(rk, builtin.ResolveReadValue(kv, callerFrameRoot, arg))
				litSeq++
			}
			if rk != "" {
				kv.Set(keytree.RParam(frameRoot, param), kvspace.Str(rk))
			}
		}
	}
	// 写参零拷贝：.rparam 和 .wparam 指向同一调用方路径，不创建本地副本
	callerLink := keytree.FuncLib(callerFrameRoot)
	addr0 := extractAddr0(pc)
	for i, ret := range funcSig.Returns {
		wSlot := fmt.Sprintf("%s/[%d,%d]", callerLink, addr0, i+1)
		wTargetVal, _ := kv.Get(wSlot)
		wTarget := string(wTargetVal.RawBytes())
		if wTarget == "" {
			continue
		}
		wk := resolveReadPath(kv, callerFrameRoot, wTarget)
		if wk == "" {
			continue
		}
		kv.Set(keytree.RParam(frameRoot, ret.Name), kvspace.Str(wk))
		kv.Set(keytree.WParam(frameRoot, ret.Name), kvspace.Str(wk))
	}
	if len(params) > 0 {
		kv.Set(keytree.FrameRO(frameRoot), kvspace.Str(strings.Join(params, ",")))
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

	kv.Unlink(keytree.FuncLib(frameRoot))
	kv.DelTree(frameRoot)
	return nextPC, ""
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

	return keytree.EntryPC(vthreadRoot)
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
func resolveWritePath(kv kvspace.KVSpace, framePath, name string) string {
	rk := frameSlotKey(framePath, name)
	if r, _ := kv.Get(framePath + "/.wparam/" + name); !r.IsNil() {
		return r.Str()
	}
	return rk
}

func resolveReadPath(kv kvspace.KVSpace, framePath, name string) string {
	if isLiteral(name) { return "" }
	if r, _ := kv.Get(framePath + "/.rparam/" + name); !r.IsNil() {
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
		if val[0] >= '0' && val[0] <= '9' || (val[0] == '-' && len(val) > 1) { kind = "int" }
	}
	return kvspace.Raw(kind, []byte(val))
}

func isLiteral(s string) bool {
	if s == "" { return false }
	return s[0] == '"' || s[0] == '/' || s == "true" || s == "false" ||
		(s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && len(s) > 1)
}

func extractAddr0(pc string) int {
	idx := strings.LastIndex(pc, "/[")
	if idx < 0 { return 0 }
	s := strings.Trim(pc[idx+1:], "[]")
	n, _ := strconv.Atoi(strings.Split(s, ",")[0])
	return n
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
	return frameRoot + "/" + slot // 裸标识符
}

func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	typeMap := lower.InferTypes(fn)
	// 清除旧函数数据：旧指令的读/写槽数量可能多于新函数，
	// 若不清除则旧槽（如 [0,-1]、[0,-2]）会被新执行错误读取。
	kv.DelTree(keytree.LibFunc(pkg, fn.Sig.Name))
	kv.Set(keytree.Src(pkg, fn.Sig.Name), kvspace.Str(fn.FullText()))
	kv.Set(keytree.LibSrc(pkg, fn.Sig.Name), kvspace.Str(fn.FullText())) // fix-034: /lib/<pkg>/<name>.src
	kv.Set(keytree.LibFunc(pkg, fn.Sig.Name), kvspace.Str(fn.Sig.String()))
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body, typeMap)
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

// Package layoutcode 将 AST 布局到 KV 空间的执行层。
//
// 存储约定：
//   /src/<pkg>/<name>              函数完整源码文本（可读，原始）
//   /func/<pkg>/<name>             编译后签名（FuncSig.String() 序列化形式）
//   /func/<pkg>/<name>/[i,j]       编译后指令
//   /func/<pkg>/<name>/<label>/    基本块子路径
//   /func/.idx/<name>              函数名 → pkg 反向索引
package layoutcode

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/ast"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/op"
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
			kv.Set(fmt.Sprintf("%s/[%d,0]", prefix, n), opcode)
		}
		for j, r := range reads {
			kv.Set(fmt.Sprintf("%s/[%d,-%d]", prefix, n, j+1), r)
		}
		for j, w := range s.Writes {
			kv.Set(fmt.Sprintf("%s/[%d,%d]", prefix, n, j+1), w)
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

// HandleCall 执行 CALL：读取签名、参数绑定、复制指令到子栈。
//
// pc 为调用指令的绝对路径（如 /vthread/42/[3,0]）。
// 返回子栈第一条指令的绝对 PC（pc + "/[0,0]"），
// 失败时返回 pc 原值（调用方据此判断失败）。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction, tail bool) string {
	vtid := keytree.VtidFromPC(pc)
	funcName := inst.Reads[0]

	// 查包路径
	pkg, err := kv.Get(keytree.FuncIdx(funcName))
	if err != nil || pkg == "" {
		vthread.SetError(ctx, kv, vtid, pc, "func not found: "+funcName)
		return pc
	}
	funcKey := keytree.Func(pkg, funcName)

	// 读签名（存在 /func/<pkg>/<name> 的 value 上）
	sig, err := kv.Get(funcKey)
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "func signature not found: "+funcName)
		return pc
	}
	funcSig := parser.ParseFuncSig(sig)

	// 构建参数绑定
	bindings := make(map[string]string)
	for i, p := range funcSig.ParamNames() {
		if i+1 < len(inst.Reads) {
			bindings[p] = inst.Reads[i+1]
			bindings["./"+p] = inst.Reads[i+1]
		}
	}
	returns := funcSig.ReturnNames()
	for i, p := range returns {
		if i < len(inst.Writes) {
			bindings[p] = inst.Writes[i]
			bindings["./"+p] = inst.Writes[i]
		}
	}

	// pc 即帧根目录（绝对路径），直接复制指令到此路径下
	root := pc
	count := copyFunc(ctx, kv, funcKey, root, bindings)

	// 追加 return 指令（若函数体已含 return，此处为安全兜底）
	if len(returns) > 0 {
		retRef := returns[0]
		if !strings.HasPrefix(retRef, "/") {
			retRef = "./" + retRef
		}
		kv.Set(fmt.Sprintf("%s/[%d,0]", root, count), "return")
		kv.Set(fmt.Sprintf("%s/[%d,-1]", root, count), retRef)
	} else {
		kv.Set(fmt.Sprintf("%s/[%d,0]", root, count), "return")
	}
	return pc + "/[0,0]"
}

// copyFunc 递归复制 srcPrefix 下的所有指令到 dstPrefix，替换 bindings。
func copyFunc(ctx context.Context, kv kvspace.KVSpace, srcPrefix, dstPrefix string, bindings map[string]string) int {
	children, _ := kv.List(srcPrefix)
	idx := 0
	seen := map[string]bool{}
	for _, name := range children {
		k := srcPrefix + "/" + name
		suffix := name
		// Block label 子路径 → 递归复制
		if !strings.HasPrefix(suffix, "[") && !strings.Contains(suffix, "/") {
			idx += copyFunc(ctx, kv, k, dstPrefix, bindings)
			continue
		}
		if strings.Contains(suffix, "/") {
			label := suffix[:strings.Index(suffix, "/")]
			if !seen[label] {
				seen[label] = true
				idx += copyFunc(ctx, kv, srcPrefix+"/"+label, dstPrefix, bindings)
			}
			continue
		}
		if !strings.Contains(suffix, ",0]") {
			continue
		}
		val, err := kv.Get(k)
		if err != nil {
			continue
		}
		if v, ok := bindings[val]; ok {
			val = v
		}
		kv.Set(fmt.Sprintf("%s/[%d,0]", dstPrefix, idx), val)

		slotIdx := suffix[1:strings.Index(suffix, ",")]
		for j := 1; j <= 10; j++ {
			rk := fmt.Sprintf("%s/[%s,-%d]", srcPrefix, slotIdx, j)
			if rv, err := kv.Get(rk); err == nil && rv != "" {
				if v, ok := bindings[rv]; ok {
					rv = v
				}
				kv.Set(fmt.Sprintf("%s/[%d,-%d]", dstPrefix, idx, j), rv)
			}
			wk := fmt.Sprintf("%s/[%s,%d]", srcPrefix, slotIdx, j)
			if wv, err := kv.Get(wk); err == nil && wv != "" {
				if v, ok := bindings[wv]; ok {
					wv = v
				}
				kv.Set(fmt.Sprintf("%s/[%d,%d]", dstPrefix, idx, j), wv)
			}
		}
		idx++
	}
	return idx
}

// RegisterBlocks 为函数体内所有 BlockStmt label 注册编译后子函数签名。
// 写入 /func/<pkg>/<name>/<label>，供 br/goto 运行时查找。
func RegisterBlocks(kv kvspace.KVSpace, pkg, parent string, body []ast.Stmt) {
	for _, st := range body {
		if b, ok := st.(*ast.BlockStmt); ok {
			blockKey := keytree.Func(pkg, parent+"/"+b.Label)
			kv.Set(blockKey, "def "+b.Label+"() -> ()")
			kv.Set(keytree.FuncIdx(parent+"/"+b.Label), pkg)
			RegisterBlocks(kv, pkg, parent+"/"+b.Label, b.Body)
		}
	}
}

// WriteFunc 完成一个函数的全部 KV 写入：
//  1. 源码写入 /src/<pkg>/<name>（完整文本）
//  2. 编译签名写入 /func/<pkg>/<name>
//  3. 编译 body 指令写入 /func/<pkg>/<name>/[i,j]
//  4. 块标签写入 /func/<pkg>/<name>/<label>/
//  5. 反向索引写入 /func/.idx/<name>
func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) {
	kv.Set(keytree.Src(pkg, fn.Sig.Name), fn.FullText())
	kv.Set(keytree.Func(pkg, fn.Sig.Name), fn.Sig.String())
	WriteBody(kv, pkg, fn.Sig.Name, fn.Body)
	RegisterBlocks(kv, pkg, fn.Sig.Name, fn.Body)
	kv.Set(keytree.FuncIdx(fn.Sig.Name), pkg)
}

// HandleReturn 处理 RETURN：回传值，删除当前帧，恢复父帧 PC。
//
// pc 为 return 指令的绝对路径（如 /vthread/42/[3,0]/[1,0]）。
// 返回值：
//   - ("", retVal) — 顶层 return（vthread 完成）；retVal 为 inst.Reads[0] 对应的值，
//     供调用方传入 vthread.SetDone(vtid, retVal)；若无读槽则 retVal=""（→ SetDone 默认 "ok"）
//   - (nextPC, "") — 嵌套 return；nextPC 为父帧下一条指令的绝对 PC
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, pc string, inst *op.Instruction) (nextPC, retVal string) {
	vtid := keytree.VtidFromPC(pc)
	vthreadRoot := keytree.VThread(vtid) // /vthread/<vtid>

	// framePC = 当前帧目录（去掉最后一个 [i,0] 段）
	lastSlash := strings.LastIndex(pc, "/")
	if lastSlash < 0 {
		return "", "" // 不应发生（绝对路径必有 /）
	}
	framePC := pc[:lastSlash] // e.g. /vthread/42/[3,0] 或 /vthread/42

	// 顶层 return：帧目录就是 vthread 根
	if framePC == vthreadRoot {
		// 读取返回值（如 ./status → /vthread/<vtid>/status，或 ./.status 等）
		if len(inst.Reads) > 0 && strings.HasPrefix(inst.Reads[0], "./") {
			srcKey := framePC + "/" + inst.Reads[0][2:]
			if v, e := kv.Get(srcKey); e == nil {
				retVal = v
			}
		}
		return "", retVal // 信号：调用方调 SetDone(vtid, retVal)
	}

	// 嵌套 return：复制返回值到父帧
	// inst.Reads[0] 已由 HandleCall 绑定为调用方的写目标（如 ./result）
	if len(inst.Reads) > 0 && strings.HasPrefix(inst.Reads[0], "./") {
		srcKey := framePC + "/" + inst.Reads[0][2:]
		if v, e := kv.Get(srcKey); e == nil && len(inst.Writes) > 0 && strings.HasPrefix(inst.Writes[0], "./") {
			// 父帧目录 = framePC 去掉最后一段
			parentFrameLastSlash := strings.LastIndex(framePC, "/")
			if parentFrameLastSlash > 0 {
				parentFrame := framePC[:parentFrameLastSlash]
				kv.Set(parentFrame+"/"+inst.Writes[0][2:], v)
			} else {
				// 父帧是 vthread 根
				kv.Set(vthreadRoot+"/"+inst.Writes[0][2:], v)
			}
		}
	}

	// 清理当前帧
	kv.DelR(framePC)

	return op.NextPC(framePC), ""
}

// Package layoutcode 将 AST 布局到 KV 空间的执行层。
//
// 存储约定：
//   /src/<pkg>/<name>              函数完整源码文本（可读，原始）
//   /func/<pkg>/<name>             编译后签名（供 parseSig 读取）
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
	"kvlang/internal/vthread"
)

// WriteBody 将 []Stmt 写入 /func/<pkg>/<name>/ 下的结构化 KV（编译后指令）。
func WriteBody(kv kvspace.KVSpace, pkg, name string, body []ast.Stmt) error {
	prefix := keytree.FuncCompiled(pkg, name)
	return writeStmts(kv, prefix, body)
}

func writeStmts(kv kvspace.KVSpace, prefix string, stmts []ast.Stmt) error {
	idx := 0
	for _, st := range stmts {
		st.SetKV(kv, prefix, &idx)
	}
	return nil
}

// HandleCall 执行 CALL：读取签名、参数绑定、复制指令到子栈。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction, tail bool) string {
	funcName := inst.Reads[0]

	// 查包路径
	pkg, err := kv.Get(keytree.FuncIdx(funcName))
	if err != nil || pkg == "" {
		vthread.SetError(ctx, kv, vtid, pc, "func not found: "+funcName)
		return pc
	}
	funcKey := keytree.FuncCompiled(pkg, funcName)

	// 读签名（存在 /func/<pkg>/<name> 的 value 上）
	sig, err := kv.Get(funcKey)
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "func signature not found: "+funcName)
		return pc
	}
	params := parseSig(sig)

	// 构建参数绑定
	bindings := make(map[string]string)
	for i, p := range params.Reads {
		if i+1 < len(inst.Reads) {
			bindings[p] = inst.Reads[i+1]
			bindings["./"+p] = inst.Reads[i+1]
		}
	}
	for i, p := range params.Writes {
		if i < len(inst.Writes) {
			bindings[p] = inst.Writes[i]
			bindings["./"+p] = inst.Writes[i]
		}
	}

	// 复制指令到子栈，替换形参
	root := keytree.VThreadSub(vtid, pc)
	count := copyFunc(ctx, kv, funcKey, root, bindings)

	// 追加 return 指令
	if len(params.Writes) > 0 {
		retRef := params.Writes[0]
		if !strings.HasPrefix(retRef, "/") {
			retRef = "./" + retRef
		}
		kv.Set(fmt.Sprintf("%s[%d,0]", root, count), "return")
		kv.Set(fmt.Sprintf("%s[%d,-1]", root, count), retRef)
	} else {
		kv.Set(fmt.Sprintf("%s[%d,0]", root, count), "return")
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
		kv.Set(fmt.Sprintf("%s[%d,0]", dstPrefix, idx), val)

		slotIdx := suffix[1:strings.Index(suffix, ",")]
		for j := 1; j <= 10; j++ {
			rk := fmt.Sprintf("%s/[%s,-%d]", srcPrefix, slotIdx, j)
			if rv, err := kv.Get(rk); err == nil && rv != "" {
				if v, ok := bindings[rv]; ok {
					rv = v
				}
				kv.Set(fmt.Sprintf("%s[%d,-%d]", dstPrefix, idx, j), rv)
			}
			wk := fmt.Sprintf("%s/[%s,%d]", srcPrefix, slotIdx, j)
			if wv, err := kv.Get(wk); err == nil && wv != "" {
				if v, ok := bindings[wv]; ok {
					wv = v
				}
				kv.Set(fmt.Sprintf("%s[%d,%d]", dstPrefix, idx, j), wv)
			}
		}
		idx++
	}
	return idx
}

func parseSig(sig string) ast.FormalParams {
	var fp ast.FormalParams
	sig = strings.TrimSpace(sig)
	if strings.HasPrefix(sig, "def ") {
		sig = sig[4:]
	}
	arrow := strings.Index(sig, "->")
	if arrow < 0 {
		return fp
	}
	left := strings.TrimSpace(sig[:arrow])
	right := strings.TrimSpace(sig[arrow+2:])
	if lp := strings.Index(left, "("); lp >= 0 {
		if rp := strings.LastIndex(left, ")"); rp > lp {
			for _, p := range strings.Split(left[lp+1:rp], ",") {
				name := strings.TrimSpace(p)
				if colon := strings.Index(name, ":"); colon >= 0 {
					name = name[:colon]
				}
				if name != "" {
					fp.Reads = append(fp.Reads, strings.TrimSpace(name))
				}
			}
		}
	}
	right = strings.Trim(right, "()")
	for _, p := range strings.Split(right, ",") {
		name := strings.TrimSpace(p)
		if colon := strings.Index(name, ":"); colon >= 0 {
			name = name[:colon]
		}
		if name != "" {
			fp.Writes = append(fp.Writes, strings.TrimSpace(name))
		}
	}
	return fp
}

// RegisterBlocks 为函数体内所有 BlockStmt label 注册编译后子函数签名。
// 写入 /func/<pkg>/<name>/<label>，供 br/goto 运行时查找。
func RegisterBlocks(kv kvspace.KVSpace, pkg, parent string, body []ast.Stmt) {
	for _, st := range body {
		if b, ok := st.(*ast.BlockStmt); ok {
			blockKey := keytree.FuncCompiled(pkg, parent+"/"+b.Label)
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
func WriteFunc(kv kvspace.KVSpace, pkg string, fn *ast.Func) error {
	// 1. 源码
	if err := fn.Register(kv, pkg); err != nil {
		return err
	}
	// 2. 编译签名
	kv.Set(keytree.FuncCompiled(pkg, fn.Name), fn.Signature)
	// 3. 编译 body
	WriteBody(kv, pkg, fn.Name, fn.Body)
	// 4. 块标签
	RegisterBlocks(kv, pkg, fn.Name, fn.Body)
	// 5. 反向索引
	kv.Set(keytree.FuncIdx(fn.Name), pkg)
	return nil
}

// HandleReturn 处理 RETURN：回传值，删除子栈，恢复父栈 PC。
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) string {
	lastSlash := strings.LastIndex(pc, "/")
	if lastSlash < 0 {
		return pc
	}
	parentPC := pc[:lastSlash]

	if len(inst.Reads) > 0 && strings.HasPrefix(inst.Reads[0], "./") {
		srcKey := keytree.VThreadAt(vtid, inst.Reads[0][2:])
		if v, e := kv.Get(srcKey); e == nil && len(inst.Writes) > 0 {
			slotKey := keytree.VThreadAt(vtid, inst.Writes[0][2:])
			kv.Set(slotKey, v)
		}
	}

	children, _ := kv.List(keytree.VThreadAt(vtid, parentPC))
	for _, c := range children {
		kv.Del(keytree.VThreadAt(vtid, parentPC) + "/" + c)
	}
	return op.NextPC(parentPC)
}

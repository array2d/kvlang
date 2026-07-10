// Package layoutcode 将 AST 布局到 KV 空间的执行层。
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

// WriteBody 将 []Stmt 写入 /src/func/<name>/ 下的结构化 KV。
// Instruction → [i,0]/[i,-1]/[i,1] 格式
// BlockStmt  → <label>/ 子目录
func WriteBody(kv kvspace.KVSpace, name string, body []ast.Stmt) error {
	prefix := keytree.SrcFunc(name)
	return writeStmts(kv, prefix, body)
}

func writeStmts(kv kvspace.KVSpace, prefix string, stmts []ast.Stmt) error {
	idx := 0
	for _, st := range stmts {
		st.SetKV(kv, prefix, &idx)
	}
	return nil
}

// HandleCall 执行 CALL: 读取签名, 参数绑定, 复制指令到子栈。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction, tail bool) string {
	funcName := inst.Reads[0]

	// 读签名
	sig, err := kv.Get(keytree.SrcFunc(funcName))
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, "func not found: "+funcName)
		return pc
	}
	params := parseSig(sig)

	// 构建参数绑定
	bindings := make(map[string]string)
	for i, p := range params.Reads {
		if i+1 < len(inst.Reads) { bindings[p] = inst.Reads[i+1] }
	}
	for i, p := range params.Writes {
		if i < len(inst.Writes) { bindings[p] = inst.Writes[i] }
	}

	// 复制指令到子栈, 替换形参
	root := keytree.VThreadSub(vtid, pc)
	count := copyFunc(ctx, kv, keytree.SrcFunc(funcName), root, bindings)

	// 追加 return 指令
	if len(params.Writes) > 0 {
		retRef := params.Writes[0]
		if !strings.HasPrefix(retRef, "/") { retRef = "./" + retRef }
		kv.Set(fmt.Sprintf("%s[%d,0]", root, count), "return", 0)
		kv.Set(fmt.Sprintf("%s[%d,-1]", root, count), retRef, 0)
	} else {
		kv.Set(fmt.Sprintf("%s[%d,0]", root, count), "return", 0)
	}
	return pc + "/[0,0]"
}

// copyFunc 递归复制 srcPrefix 下的所有指令到 dstPrefix，替换 bindings。
// 支持 block label 子路径: label/[i,0] 作为 call 的目标。
func copyFunc(ctx context.Context, kv kvspace.KVSpace, srcPrefix, dstPrefix string, bindings map[string]string) int {
	keys, _ := kv.Keys(srcPrefix + "/*")
	idx := 0
	seen := map[string]bool{}
	for _, k := range keys {
		suffix := strings.TrimPrefix(k, srcPrefix+"/")
		// Block label 子路径 → 递归复制
		if !strings.HasPrefix(suffix, "[") && !strings.Contains(suffix, "/") {
			idx += copyFunc(ctx, kv, k, dstPrefix, bindings)
			continue
		}
		if strings.Contains(suffix, "/") {
			label := suffix[:strings.Index(suffix, "/")]
			if !seen[label] { seen[label] = true; idx += copyFunc(ctx, kv, srcPrefix+"/"+label, dstPrefix, bindings) }
			continue
		}
		// 检查是不是 [n,0] 格式
		if !strings.Contains(suffix, ",0]") { continue }
		val, err := kv.Get(k)
		if err != nil { continue }
		if v, ok := bindings[val]; ok { val = v }
		kv.Set(fmt.Sprintf("%s[%d,0]", dstPrefix, idx), val, 0)

		slotIdx := suffix[1:strings.Index(suffix, ",")]
		for j := 1; j <= 10; j++ {
			rk := fmt.Sprintf("%s/[%s,-%d]", srcPrefix, slotIdx, j)
			if rv, err := kv.Get(rk); err == nil && rv != "" {
				if v, ok := bindings[rv]; ok { rv = v }
				kv.Set(fmt.Sprintf("%s[%d,-%d]", dstPrefix, idx, j), rv, 0)
			}
			wk := fmt.Sprintf("%s/[%s,%d]", srcPrefix, slotIdx, j)
			if wv, err := kv.Get(wk); err == nil && wv != "" {
				if v, ok := bindings[wv]; ok { wv = v }
				kv.Set(fmt.Sprintf("%s[%d,%d]", dstPrefix, idx, j), wv, 0)
			}
		}
		idx++
	}
	return idx
}

func parseSig(sig string) ast.FormalParams {
	var fp ast.FormalParams
	sig = strings.TrimSpace(sig)
	if strings.HasPrefix(sig, "def ") { sig = sig[4:] }
	arrow := strings.Index(sig, "->")
	if arrow < 0 { return fp }
	left := strings.TrimSpace(sig[:arrow])
	right := strings.TrimSpace(sig[arrow+2:])
	if lp := strings.Index(left, "("); lp >= 0 {
		if rp := strings.LastIndex(left, ")"); rp > lp {
			for _, p := range strings.Split(left[lp+1:rp], ",") {
				name := strings.TrimSpace(p)
				if colon := strings.Index(name, ":"); colon >= 0 { name = name[:colon] }
				if name != "" { fp.Reads = append(fp.Reads, strings.TrimSpace(name)) }
			}
		}
	}
	right = strings.Trim(right, "()")
	for _, p := range strings.Split(right, ",") {
		name := strings.TrimSpace(p)
		if colon := strings.Index(name, ":"); colon >= 0 { name = name[:colon] }
		if name != "" { fp.Writes = append(fp.Writes, strings.TrimSpace(name)) }
	}
	return fp
}

// HandleReturn 处理 RETURN: 回传值, 删除子栈, 恢复父栈 PC。
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) string {
	lastSlash := strings.LastIndex(pc, "/")
	if lastSlash < 0 { return pc }
	parentPC := pc[:lastSlash]

	if len(inst.Reads) > 0 && strings.HasPrefix(inst.Reads[0], "./") {
		srcKey := keytree.VThreadAt(vtid, inst.Reads[0][2:])
		if v, e := kv.Get(srcKey); e == nil && len(inst.Writes) > 0 {
			slotKey := keytree.VThreadAt(vtid, inst.Writes[0][2:])
			kv.Set(slotKey, v, 0)
		}
	}

	keys, _ := kv.Keys(keytree.VThreadAt(vtid, parentPC) + "/*")
	if len(keys) > 0 { kv.Del(keys...) }
	return op.NextPC(parentPC)
}

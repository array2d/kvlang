// Package codegen 将 AST 翻译为执行层指令 (eager translation on CALL)。
package codegen

import (
	"context"
	"fmt"
	"sort"
	"strconv"
	"strings"

	"kvlang/internal/ir"
	"kvlang/internal/logx"
	"kvlang/internal/parser"
	"kvlang/internal/op/dispatch"
	"kvlang/internal/vthread"
	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
)

// HandleCall 执行 CALL 指令的 eager 翻译，返回子栈第一条指令的 PC。
func HandleCall(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *ir.Instruction) string {
	funcName := inst.Reads[0]
	backend := dispatch.DetermineBackend(ctx, kv, funcName)

	sig, err := kv.Get(ctx, keytree.OpBackendFunc(backend, funcName))
	if err != nil {
		sig, err = kv.Get(ctx, keytree.SrcFunc(funcName))
		if err != nil {
			msg := fmt.Sprintf("func %s not found", funcName)
			logx.Warn("[%s] CALL: %s", vtid, msg)
			vthread.SetError(ctx, kv, vtid, pc, msg)
			return pc
		}
	}

	formalParams := parser.ParseSignature(sig)
	bindings := make(map[string]string)
	for i, param := range formalParams.Reads {
		if i+1 < len(inst.Reads) {
			bindings[param] = inst.Reads[i+1]
		}
	}
	for i, param := range formalParams.Writes {
		if i < len(inst.Writes) {
			bindings[param] = inst.Writes[i]
		}
	}

	compiled := mgetAll(ctx, kv, keytree.OpBackendFunc(backend, funcName))
	if len(compiled) == 0 {
		compiled = mgetAll(ctx, kv, keytree.SrcFunc(funcName))
	}

	substackRoot := keytree.VThreadSub(vtid, pc)
	pipe := kv.Pipeline()
	bodyCount := len(compiled)
	for i, kvlangLine := range compiled {
		parsed, err := parser.ParseLine(kvlangLine)
		if err != nil {
			msg := fmt.Sprintf("parse body[%d]: %v", i, err)
			logx.Warn("[%s] CALL: %s", vtid, msg)
			vthread.SetError(ctx, kv, vtid, pc, msg)
			return pc
		}
		replaceParams(parsed.Reads, bindings)
		replaceParams(parsed.Writes, bindings)
		pipe.Set(ctx, fmt.Sprintf("%s[%d,0]", substackRoot, i), parsed.Opcode, 0)
		for j, r := range parsed.Reads {
			pipe.Set(ctx, fmt.Sprintf("%s[%d,-%d]", substackRoot, i, j+1), r, 0)
		}
		for j, w := range parsed.Writes {
			pipe.Set(ctx, fmt.Sprintf("%s[%d,%d]", substackRoot, i, j+1), w, 0)
		}
	}

	retIdx := bodyCount
	if len(formalParams.Writes) > 0 {
		retRef := formalParams.Writes[0]
		if !strings.HasPrefix(retRef, "/") {
			retRef = "./" + retRef
		}
		pipe.Set(ctx, fmt.Sprintf("%s[%d,0]", substackRoot, retIdx), "return", 0)
		pipe.Set(ctx, fmt.Sprintf("%s[%d,-1]", substackRoot, retIdx), retRef, 0)
	} else {
		pipe.Set(ctx, fmt.Sprintf("%s[%d,0]", substackRoot, retIdx), "return", 0)
	}

	if _, err := pipe.Exec(ctx); err != nil {
		msg := fmt.Sprintf("CALL pipeline: %v", err)
		logx.Warn("[%s] CALL: %s", vtid, msg)
		vthread.SetError(ctx, kv, vtid, pc, msg)
		return pc
	}
	return pc + "/[0,0]"
}

// HandleReturn 处理 RETURN: 回传返回值, 删除子栈, 恢复父栈 PC。
func HandleReturn(ctx context.Context, kv kvspace.KVSpace, vtid, pc string) string {
	lastSlash := strings.LastIndex(pc, "/")
	if lastSlash < 0 {
		return pc
	}
	parentPC := pc[:lastSlash]

	inst, err := ir.Decode(ctx, kv, vtid, pc)
	if err == nil {
		parentInst, pErr := ir.Decode(ctx, kv, vtid, parentPC)
		if pErr == nil && len(parentInst.Writes) > 0 && len(inst.Reads) > 0 {
			retSlot := parentInst.Writes[0]
			retRef := inst.Reads[0]
			retVal := retRef
			if strings.HasPrefix(retRef, "./") {
				srcKey := keytree.VThreadAt(vtid, retRef[2:])
				if v, e := kv.Get(ctx, srcKey); e == nil {
					retVal = v
				}
			}
			if strings.HasPrefix(retSlot, "./") {
				slotKey := keytree.VThreadAt(vtid, retSlot[2:])
				kv.Set(ctx, slotKey, retVal, 0)
			}
		}
	}

	keys, _ := kv.Keys(ctx, keytree.VThreadAt(vtid, parentPC)+"/*")
	if len(keys) > 0 {
		kv.Del(ctx, keys...)
	}
	return ir.NextPC(parentPC)
}

func replaceParams(params []string, bindings map[string]string) {
	for i, p := range params {
		if v, ok := bindings[p]; ok {
			params[i] = v
		}
	}
}

func mgetAll(ctx context.Context, kv kvspace.KVSpace, base string) []string {
	keys, err := kv.Keys(ctx, base+"/*")
	if err != nil {
		return nil
	}
	type ik struct {
		key   string
		index int
	}
	var sorted []ik
	for _, k := range keys {
		suffix := strings.TrimPrefix(k, base+"/")
		n, err := strconv.Atoi(suffix)
		if err != nil {
			continue
		}
		sorted = append(sorted, ik{key: k, index: n})
	}
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].index < sorted[j].index })
	ordered := make([]string, len(sorted))
	for i, s := range sorted {
		ordered[i] = s.key
	}
	if len(ordered) == 0 {
		return nil
	}
	vals, _ := kv.MGet(ctx, ordered...)
	result := make([]string, 0, len(vals))
	for _, v := range vals {
		if s, ok := v.(string); ok {
			result = append(result, s)
		}
	}
	return result
}

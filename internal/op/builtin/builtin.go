// Package builtin 提供 VM 原生求值引擎。
//
// Native() 是入口，处理标量类型指令 (整型/浮点/布尔/字符串)，
// 不做 GPU 调度，不经过 op-plat，直接在 VM 进程内完成。
package builtin

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/device"
	"kvlang/internal/ir"
	"kvlang/internal/logx"
	"kvlang/internal/vthread"
	"kvlang/internal/keytree"

	"kvlang/internal/kvspace"
)

// Native 直接求值基础类型运算指令，不经过 op-plat。
func Native(ctx context.Context, kv kvspace.KVSpace, vtid string, pc string, inst *ir.Instruction) error {
	inputs := make([]nativeValue, 0, len(inst.Reads))
	for _, r := range inst.Reads {
		var raw string
		if isRelative(r) {
			key := keytree.VThreadAt(vtid, r[2:])
			val, err := kv.Get(ctx, key)
			if err != nil {
				msg := fmt.Sprintf("native read %s: %v", key, err)
				vthread.SetError(ctx, kv, vtid, pc, msg)
				return fmt.Errorf("%s", msg)
			}
			raw = val
		} else {
			raw = r
		}
		inputs = append(inputs, parseNativeValue(raw))
	}

	result, err := evalNative(inst.Opcode, inputs)
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, err.Error())
		return err
	}

	// str.set → 将字符串值写入相对路径 key
	if inst.Opcode == OpStrSet {
		val := ""
		if len(inputs) > 0 {
			val = inputs[0].String()
		}
		if len(inst.Writes) > 0 {
			wKey := resolveWriteKey(vtid, inst.Writes[0])
			if err := kv.Set(ctx, wKey, val, 0); err != nil {
				msg := fmt.Sprintf("str.set %s: %v", wKey, err)
				vthread.SetError(ctx, kv, vtid, pc, msg)
				return fmt.Errorf("%s", msg)
			}
		}
		logx.Debug("[%s] str.set %q -> %s", vtid, val, inst.Writes)
		vthread.Set(ctx, kv, vtid, ir.NextPC(pc), "running")
		return nil
	}

	// print → stdout, cerr → stderr
	if inst.Opcode == OpPrint || inst.Opcode == OpCerr {
		stream := "stdout"
		if inst.Opcode == OpCerr {
			stream = "stderr"
		}
		ts := device.ResolveTerm(ctx, kv, vtid, stream)
		parts := make([]string, len(inputs))
		for i, v := range inputs {
			parts[i] = v.String()
		}
		line := strings.Join(parts, " ")
		logx.Debug("[%s] %s %s", vtid, strings.ToUpper(inst.Opcode), line)
		if !ts.IsZero() {
			if err := device.WriteTerm(ctx, ts, line); err != nil {
				logx.Warn("[%s] write %s: %v", vtid, stream, err)
			}
		}
		vthread.Set(ctx, kv, vtid, ir.NextPC(pc), "running")
		return nil
	}

	// input → 从终端 stdin 读取
	if inst.Opcode == OpInput {
		if len(inputs) > 0 {
			prompt := inputs[0].String()
			outTS := device.ResolveTerm(ctx, kv, vtid, "stdout")
			if !outTS.IsZero() {
				device.WriteTerm(ctx, outTS, prompt)
			}
		}
		inTS := device.ResolveTerm(ctx, kv, vtid, "stdin")
		var val string
		if !inTS.IsZero() {
			var inErr error
			val, inErr = device.ReadTerm(ctx, inTS)
			if inErr != nil {
				vthread.SetError(ctx, kv, vtid, pc, inErr.Error())
				return inErr
			}
		}
		if len(inst.Writes) > 0 {
			wKey := resolveWriteKey(vtid, inst.Writes[0])
			if err := kv.Set(ctx, wKey, val, 0); err != nil {
				msg := fmt.Sprintf("native write %s: %v", wKey, err)
				vthread.SetError(ctx, kv, vtid, pc, msg)
				return fmt.Errorf("%s", msg)
			}
		}
		logx.Debug("[%s] INPUT = %s", vtid, val)
		vthread.Set(ctx, kv, vtid, ir.NextPC(pc), "running")
		return nil
	}

	// 默认：写回计算结果
	if len(inst.Writes) > 0 {
		outKey := resolveWriteKey(vtid, inst.Writes[0])
		if err := kv.Set(ctx, outKey, result.String(), 0); err != nil {
			msg := fmt.Sprintf("native write %s: %v", outKey, err)
			vthread.SetError(ctx, kv, vtid, pc, msg)
			return fmt.Errorf("%s", msg)
		}
	}

	logx.Debug("[%s] NATIVE %s %v = %s", vtid, inst.Opcode, inputs, result.String())
	vthread.Set(ctx, kv, vtid, ir.NextPC(pc), "running")
	return nil
}

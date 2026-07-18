// Package dispatch 负责算子分发到 /sys/op/<backend>/<n>/cmd。
package dispatch

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"kvlang/internal/keytree"
	"github.com/array2d/kvlang-go"
	"kvlang/internal/logx"
	"kvlang/internal/op"
	"kvlang/internal/vthread"
)

type ParamRef struct {
	Key     string                 `json:"key"`
	Dtype   string                 `json:"dtype,omitempty"`
	Shape   []int                  `json:"shape,omitempty"`
	Address map[string]interface{} `json:"address,omitempty"`
}

type OpTask struct {
	Vtid    string                 `json:"vtid"`
	PC      string                 `json:"pc"`
	Opcode  string                 `json:"opcode"`
	Inputs  []ParamRef             `json:"inputs"`
	Outputs []ParamRef             `json:"outputs"`
	Params  map[string]interface{} `json:"params,omitempty"`
}


func isAbsolute(param string) bool {
	return len(param) > 0 && param[0] == '/'
}

func isNumber(param string) bool {
	if len(param) == 0 {
		return false
	}
	for _, c := range param {
		if c >= '0' && c <= '9' || c == '.' || c == 'e' || c == 'E' {
			continue
		}
		return false
	}
	return true
}

func resolveParam(ctx context.Context, kv kvspace.KVSpace, vtid, param string) ParamRef {
	ref := ParamRef{Key: param}
	resolvedKey := param
	if !isAbsolute(param) && !isNumber(param) {
		// 裸 ident → 本帧局部变量
		resolvedKey = keytree.VThreadAt(vtid, param)
	}
	ref.Key = resolvedKey
	val, err := kv.Get(resolvedKey)
	if err != nil {
		return ref
	}
	var meta map[string]interface{}
	if json.Unmarshal([]byte(val.Str()), &meta) != nil {
		return ref
	}
	if dtype, ok := meta["dtype"].(string); ok {
		ref.Dtype = dtype
	}
	if shapeRaw, ok := meta["shape"].([]interface{}); ok {
		for _, s := range shapeRaw {
			if n, ok := s.(float64); ok {
				ref.Shape = append(ref.Shape, int(n))
			}
		}
	}
	if addr, ok := meta["address"].(map[string]interface{}); ok {
		ref.Address = addr
	}
	return ref
}

func isLiteral(s string) bool {
	if len(s) > 0 && s[0] == '/' {
		return false
	}
	return true
}

func buildOpTask(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) *OpTask {
	task := &OpTask{Vtid: vtid, PC: pc, Opcode: inst.Opcode, Params: make(map[string]interface{})}
	switch inst.Opcode {
	case "save":
		for i, r := range inst.Reads {
			if i == 0 {
				task.Inputs = append(task.Inputs, resolveParam(ctx, kv, vtid, r))
			} else {
				task.Params[fmt.Sprintf("arg%d", len(task.Params))] = r
			}
		}
	case "load":
		for _, r := range inst.Reads {
			task.Params[fmt.Sprintf("arg%d", len(task.Params))] = r
		}
		for _, w := range inst.Writes {
			task.Outputs = append(task.Outputs, resolveParam(ctx, kv, vtid, w))
		}
	case "print":
		for _, r := range inst.Reads {
			task.Inputs = append(task.Inputs, resolveParam(ctx, kv, vtid, r))
		}
	default:
		for _, r := range inst.Reads {
			if isLiteral(r) {
				task.Params[fmt.Sprintf("arg%d", len(task.Params))] = r
			} else {
				task.Inputs = append(task.Inputs, resolveParam(ctx, kv, vtid, r))
			}
		}
		for _, w := range inst.Writes {
			task.Outputs = append(task.Outputs, resolveParam(ctx, kv, vtid, w))
		}
	}
	return task
}


func parseShapeParam(raw string) []int {
	raw = strings.Trim(raw, "[] ")
	if raw == "" {
		return nil
	}
	var shape []int
	for _, s := range strings.Split(raw, ",") {
		s = strings.TrimSpace(s)
		if s == "" {
			continue
		}
		var n int
		fmt.Sscanf(s, "%d", &n)
		shape = append(shape, n)
	}
	return shape
}

// Compute 分发张量计算指令到 /sys/op/<backend>/<n>/cmd。
func Compute(ctx context.Context, kv kvspace.KVSpace, vtid, pc string, inst *op.Instruction) error {
	backend, n, err := Select(ctx, kv, inst.Opcode)
	if err != nil {
		return fmt.Errorf("route: %w", err)
	}
	task := buildOpTask(ctx, kv, vtid, pc, inst)
	cmdQueue := keytree.SysOpCmd(backend, n)
	taskJSON, _ := json.Marshal(task)
	if err := kv.Notify(cmdQueue, kvspace.Bytes(taskJSON)); err != nil {
		return fmt.Errorf("push task: %w", err)
	}
	logx.Debug("[%s] PUSH %s → %s", vtid, inst.Opcode, cmdQueue)
	vthread.Set(ctx, kv, vtid, pc, "wait")
	_, err = vthread.WaitDone(ctx, kv, vtid, 30*time.Second)
	if err != nil {
		vthread.SetError(ctx, kv, vtid, pc, err.Error())
		return err
	}
	logx.Debug("[%s] DONE %s", vtid, inst.Opcode)
	vthread.Set(ctx, kv, vtid, op.NextPC(pc), "running")
	return nil
}



package ir

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/keytree"
)

// Instruction 表示执行层 [addr0, addr1] 解码后的一条指令。
type Instruction struct {
	Opcode string   // [addr0, 0] = "+" | "call" | "return" | ...
	Reads  []string // [addr0, -1], [addr0, -2], ...
	Writes []string // [addr0, 1], [addr0, 2], ...
	PC     string   // 当前指令坐标, e.g., "[3,0]" 或 "[2,0]/[1,0]"
}

const maxParams = 10

// Decode 从 kvspace 执行层 key 解码指令。
func Decode(ctx context.Context, kv kvspace.KVSpace, vtid string, pc string) (*Instruction, error) {
	prefix, addr0 := parsePC(pc)
	keyBase := keytree.VThreadSub(vtid, prefix)

	keys := make([]string, 0, 1+maxParams*2)
	keys = append(keys, fmt.Sprintf("%s[%d,0]", keyBase, addr0))
	for i := 1; i <= maxParams; i++ {
		keys = append(keys, fmt.Sprintf("%s[%d,-%d]", keyBase, addr0, i))
		keys = append(keys, fmt.Sprintf("%s[%d,%d]", keyBase, addr0, i))
	}

	vals, err := kv.MGet(keys...)
	if err != nil {
		return nil, fmt.Errorf("decode MGET: %w", err)
	}

	inst := &Instruction{PC: pc}

	if s, ok := vals[0].(string); ok {
		inst.Opcode = s
	}

	for i := 1; i <= maxParams; i++ {
		readIdx := (i-1)*2 + 1
		writeIdx := readIdx + 1
		if readIdx < len(vals) {
			if s, ok := vals[readIdx].(string); ok && s != "" {
				inst.Reads = append(inst.Reads, s)
			}
		}
		if writeIdx < len(vals) {
			if s, ok := vals[writeIdx].(string); ok && s != "" {
				inst.Writes = append(inst.Writes, s)
			}
		}
	}

	return inst, nil
}

// DecodeFromCache 从本地缓存 map 解码 (子栈场景, 零 kvspace 访问)。
func DecodeFromCache(cache map[string]string, pc string) *Instruction {
	_, addr0 := parsePC(pc)
	inst := &Instruction{PC: pc}
	inst.Opcode = cache[fmt.Sprintf("[%d,0]", addr0)]

	for i := 1; i <= maxParams; i++ {
		key := fmt.Sprintf("[%d,-%d]", addr0, i)
		if v, ok := cache[key]; ok && v != "" {
			inst.Reads = append(inst.Reads, v)
		}
		key = fmt.Sprintf("[%d,%d]", addr0, i)
		if v, ok := cache[key]; ok && v != "" {
			inst.Writes = append(inst.Writes, v)
		}
	}
	return inst
}

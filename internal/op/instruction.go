package op

import (
	"context"
	"fmt"

	"kvlang/internal/kvspace"
	"kvlang/internal/keytree"
)

// Instruction 表示执行层 [addr0, addr1] 解码后的一条指令。
type Instruction struct {
	Opcode string   // [addr0, 0] = "+" | OpCall | OpReturn | ...
	Reads  []string // [addr0, -1], [addr0, -2], ...
	Writes []string // [addr0, 1], [addr0, 2], ...
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

	vals, err := kv.Gets(keys...)
	if err != nil {
		return nil, fmt.Errorf("decode MGET: %w", err)
	}

	inst := &Instruction{}

	if vals[0] != "" {
		inst.Opcode = vals[0]
	}

	for i := 1; i <= maxParams; i++ {
		readIdx := (i-1)*2 + 1
		writeIdx := readIdx + 1
		if readIdx < len(vals) && vals[readIdx] != "" {
			inst.Reads = append(inst.Reads, vals[readIdx])
		}
		if writeIdx < len(vals) && vals[writeIdx] != "" {
			inst.Writes = append(inst.Writes, vals[writeIdx])
		}
	}

	return inst, nil
}


package op

import (
	"context"
	"fmt"
	"strings"

	"kvlang/internal/kvspace"
)

// Instruction 表示执行层 [addr0, addr1] 解码后的一条指令。
type Instruction struct {
	Opcode string   // [addr0, 0] = "+" | OpCall | OpReturn | ...
	Reads  []string // [addr0, -1], [addr0, -2], ...
	Writes []string // [addr0, 1], [addr0, 2], ...
}

const maxParams = 10

// Decode 从 kvspace 执行层 key 解码指令。
//
// pc 为绝对路径，格式：/vthread/<vtid>/[i,0] 或 /vthread/<vtid>/[j,0]/[i,0]。
// keyBase = pc[:lastSlash]，即帧目录；addr0 = i（当前指令序号）。
func Decode(ctx context.Context, kv kvspace.KVSpace, pc string) (*Instruction, error) {
	lastSlash := strings.LastIndex(pc, "/")
	if lastSlash < 0 {
		return nil, fmt.Errorf("Decode: invalid absolute pc (no /): %q", pc)
	}
	keyBase := pc[:lastSlash]              // e.g. /vthread/42 or /vthread/42/[3,0]
	addr0 := extractAddr0(pc[lastSlash+1:]) // e.g. 0 from "[0,0]"

	keys := make([]string, 0, 1+maxParams*2)
	keys = append(keys, fmt.Sprintf("%s/[%d,0]", keyBase, addr0))
	for i := 1; i <= maxParams; i++ {
		keys = append(keys, fmt.Sprintf("%s/[%d,-%d]", keyBase, addr0, i))
		keys = append(keys, fmt.Sprintf("%s/[%d,%d]", keyBase, addr0, i))
	}

	vals, err := kv.GetMany(keys)
	if err != nil {
		return nil, fmt.Errorf("Decode MGET: %w", err)
	}

	inst := &Instruction{}
	if s := vals[0].Str(); s != "" {
		inst.Opcode = s
	}
	for i := 1; i <= maxParams; i++ {
		readIdx := (i-1)*2 + 1
		writeIdx := readIdx + 1
		if readIdx < len(vals) {
			if s := vals[readIdx].Str(); s != "" {
				inst.Reads = append(inst.Reads, s)
			}
		}
		if writeIdx < len(vals) {
			if s := vals[writeIdx].Str(); s != "" {
				inst.Writes = append(inst.Writes, s)
			}
		}
	}
	return inst, nil
}

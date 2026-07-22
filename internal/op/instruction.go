package op

import (
	"context"
	"fmt"
	"strings"

	"github.com/array2d/kvspace-go"
)

// Instruction 表示执行层 [addr0, addr1] 解码后的一条指令。
type Instruction struct {
	Opcode     string   // [addr0, 0] = "+" | OpCall | OpReturn | ...
	Reads      []string // [addr0, -1], [addr0, -2], ...
	ReadKinds  []string // 读槽的 kind（与 Reads 平行）
	Writes     []string // [addr0, 1], [addr0, 2], ...
	WriteKinds []string // 写槽的 kind（与 Writes 平行）
}

// maxParams 单条指令最大读写槽数。对齐五语言参数上限：C 标准保证≥127，取 2^7=128。
// Decode 的 MGET key 数 = 1 + 2*maxParams = 257，Redis 单次批量完全可承受。
// 超过 128 槽的指令极少见（函数参数的极端情形），超出时 Decode 返回错误而非静默截断。
const maxParams = 128

// Decode 从 kvspace 执行层 key 解码指令。
//
// pc 为绝对路径，格式：/vthread/<vtid>/[i,0] 或 /vthread/<vtid>/[j,0]/[i,0]。
// keyBase = FuncLib(FrameRoot(pc))，即 .funclib Link 目标的只读指令树根。
func Decode(ctx context.Context, kv kvspace.KVSpace, linkBase, pc string) (*Instruction, error) {
	lastSlash := strings.LastIndex(pc, "/[")
	if lastSlash < 0 {
		return nil, fmt.Errorf("Decode: invalid pc (no /[coord]): %q", pc)
	}
	addr0 := extractAddr0(pc[lastSlash+1:])
	keyBase := linkBase

	keys := make([]string, 0, 1+maxParams*2)
	keys = append(keys, fmt.Sprintf("%s/[%d,0]", keyBase, addr0))
	for i := 1; i <= maxParams; i++ {
		keys = append(keys, fmt.Sprintf("%s/[%d,-%d]", keyBase, addr0, i))
		keys = append(keys, fmt.Sprintf("%s/[%d,%d]", keyBase, addr0, i))
	}

	vals := kv.Get(keys)

	inst := &Instruction{}
	if s := string(vals[0].RawBytes()); s != "" {
		inst.Opcode = s
	}
	truncated := false
	for i := 1; i <= maxParams; i++ {
		readIdx := (i-1)*2 + 1
		writeIdx := readIdx + 1
		if readIdx < len(vals) {
			if s := string(vals[readIdx].RawBytes()); s != "" {
				inst.Reads = append(inst.Reads, s)
				inst.ReadKinds = append(inst.ReadKinds, vals[readIdx].Kind())
				if i == maxParams { truncated = true }
			}
		}
		if writeIdx < len(vals) {
			if s := string(vals[writeIdx].RawBytes()); s != "" {
				inst.Writes = append(inst.Writes, s)
				inst.WriteKinds = append(inst.WriteKinds, vals[writeIdx].Kind())
				if i == maxParams { truncated = true }
			}
		}
	}
	if truncated {
		return nil, fmt.Errorf("Decode: instruction at %s exceeds maxParams=%d slots (reads=%d, writes=%d)",
			pc, maxParams, len(inst.Reads), len(inst.Writes))
	}
	return inst, nil
}

package op

import "github.com/array2d/kvspace-go"

// Frame 指令执行的运行时上下文。
type Frame struct {
	KV   kvspace.KVSpace
	Vtid string
	PC   string
	Inst *Instruction
}


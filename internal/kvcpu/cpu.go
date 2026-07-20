// Package kvcpu 提供 KV 虚拟 CPU 执行引擎。
//
//	c := kvcpu.New(kv, vmID)
//	c.Execute(pc)
//
// CPU 通过 kvspace 作为统一内存，Fetch-Decode-Execute 循环执行 vthread。
package kvcpu

import "github.com/array2d/kvspace-go"

// CPU 是 kvlang 虚拟 CPU 的对外接口。
type CPU interface {
	Execute(pc string) error
}

// cpu 是 CPU 接口的实现，持有 kvspace 引用和 vmID。
type cpu struct {
	kv   kvspace.KVSpace
	vmID string
}

// New 创建一个与 kv 绑定的 CPU 实例。
// vmID 用于系统级通知（/sys/vm/<vmID>/err）。
// 所有 CPU 实例均内置调试支持：通过 /vthread/<vtid>/.debugger 键激活。
func New(kv kvspace.KVSpace, vmID string) CPU {
	return &cpu{kv: kv, vmID: vmID}
}

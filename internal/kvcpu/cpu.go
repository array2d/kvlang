// Package kvcpu 提供 KV 虚拟 CPU 执行引擎。
//
// 使用方式：
//
//	c := kvcpu.New(kv, vmID)
//	go c.RunWorker(ctx, 0)
//	go c.RunWorker(ctx, 1)
//
// CPU 通过 kvspace 作为统一内存，Fetch-Decode-Execute 循环执行 vthread。
package kvcpu

import "github.com/array2d/kvlang-go"

// CPU 是 kvlang 虚拟 CPU 的对外接口。
//
//	Execute  — 从给定绝对 PC 执行 vthread 直到完成或出错
//	RunWorker — 单 worker 主循环，内部调用 pick/wait/Execute
type CPU interface {
	Execute(pc string) error
	RunWorker(id int)
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

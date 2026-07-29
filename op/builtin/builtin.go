// Package builtin 提供 VM 内建算子求值引擎。
//
// 每个 op 文件通过 init() 自行注册 opcode 到全局容器：
//
//	func init() {
//	    Register("opcode", "sig", implementation)
//	}
package builtin

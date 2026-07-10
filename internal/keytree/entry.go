// /func/main 是 kvlang 程序的唯一入口。
// Loader 将待执行的入口写入此 key，VM 轮询检测后认领执行。
//
// 任意时刻最多存在一个入口 — 旧入口被 DEL 后新入口才能写入。
// 无 /func/main 时 VM 处于空闲等待状态。
package keytree

// FuncMain 返回程序唯一入口的 kvspace key: /func/main。
const FuncMain = funcPrefix + "main"

const FuncRoot = "/func"
const funcPrefix = "/func/"

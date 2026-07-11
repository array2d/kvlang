// /func/main 是 kvlang 程序的唯一入口点。
// Loader 将待执行的入口 JSON 写入此 key，VM mainWatcher 轮询后认领。
//
// /func/<pkg>/<name>    存储函数编译/lowered 后的可执行代码（签名 + body 指令）
// /func/.idx/<name>     反向索引：函数名 → 所在包路径，O(1) 查找
package keytree

const FuncRoot = "/func"
const funcPrefix = "/func/"

// FuncMain 是程序唯一入口的 kvspace key: /func/main
const FuncMain = funcPrefix + "main"

// FuncCompiled 返回 /func/<pkg>/<funcname>，存储编译/lowered 后的函数。
func FuncCompiled(pkg, name string) string { return funcPrefix + pkg + "/" + name }

// FuncIdx 返回 /func/.idx/<funcname>，函数名到包路径的反向索引。
func FuncIdx(name string) string { return funcPrefix + ".idx/" + name }

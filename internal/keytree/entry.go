package keytree

const FuncRoot = "/func"

// FuncMain 是程序唯一入口的 kvspace key: /func/main
const FuncMain = "/func/main"

// Func 返回 /func/<pkg>/<name>。
// pkg 为空时直接返回 /func/<name>（全局函数）。
func Func(pkg, name string) string {
	if pkg == "" {
		return "/func/" + name
	}
	return "/func/" + pkg + "/" + name
}

// FuncIdx 返回 /func/idx/<name>，函数名到包路径的反向索引。
func FuncIdx(name string) string { return "/func/idx/" + name }

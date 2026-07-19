package keytree

const LibRoot = "/lib"

// LibMain 是程序唯一入口的 kvspace key: /lib/main
const LibMain = "/lib/main"

// LibFunc 返回 /lib/<pkg>/<name>。
// pkg 为空时直接返回 /lib/<name>（全局函数）。
func LibFunc(pkg, name string) string {
	if pkg == "" {
		return "/lib/" + name
	}
	return "/lib/" + pkg + "/" + name
}

// LibIdx 返回 /lib/idx/<name>，函数名到包路径的反向索引。
func LibIdx(name string) string { return "/lib/idx/" + name }

// LibSrc 返回 /lib/<pkg>/<name>.src，存储函数完整源码文本（fix-034）。
func LibSrc(pkg, name string) string {
	return "/lib/" + pkg + "/" + name + ".src"
}

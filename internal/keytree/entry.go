package keytree

const LibRoot = "/lib"

// LibFunc 返回 /lib/<pkg>.<name>（pkg 非空时 . 分隔；空 = 匿名，直接 /lib/<name>）。
func LibFunc(pkg, name string) string {
	if pkg == "" {
		return "/lib/" + name
	}
	return "/lib/" + pkg + "." + name
}

// LibIdx 返回 /lib/idx/<name>，函数名到包路径的反向索引。
func LibIdx(name string) string { return "/lib/idx/" + name }

// LibSrc 返回 /lib/<pkg>.<name>.src，存储函数完整源码文本（fix-034）。
func LibSrc(pkg, name string) string {
	return "/lib/" + pkg + "." + name + ".src"
}

// LibSrcMap 返回 /lib/.srcmap，存储多文件拼接后的源码行号→文件路径映射（JSON）。
func LibSrcMap() string { return "/lib/.srcmap" }

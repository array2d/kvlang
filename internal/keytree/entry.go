package keytree

const LibRoot = "/lib"

// LibFunc 返回 /lib/<pkg>.<name>（pkg 非空时 . 分隔；空 = 匿名，直接 /lib/<name>）。
func LibFunc(pkg, funcname string) string {
	if pkg == "" {
		return "/lib/" + funcname
	}
return "/lib/" + pkg + FuncPathSep + funcname
}


// LibSrc 返回 /lib/<name>.src（pkg=""）或 /lib/<pkg>.<name>.src，存储函数源码副本。
func LibSrc(pkg, funcname string) string {
	if pkg == "" {
		return "/lib/" + funcname + SrcExt
	}
	return "/lib/" + pkg + FuncPathSep + funcname + SrcExt
}

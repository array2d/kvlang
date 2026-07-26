package keytree

const LibRoot = PathSegSep + PathSegLib

func LibFunc(pkg, name string) string {
	if pkg == "" { return LibRoot + PathSegSep + name }
	return LibRoot + PathSegSep + pkg + FuncPathSep + name
}

func LibSrc(pkg, name string) string {
	if pkg == "" { return LibRoot + PathSegSep + name + SrcExt }
	return LibRoot + PathSegSep + pkg + FuncPathSep + name + SrcExt
}

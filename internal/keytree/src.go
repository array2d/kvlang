package keytree

const SrcRoot = "/src"

// Src 返回 /src/<pkg>/<name>，存储函数完整源码文本。
// pkg 为空时直接返回 /src/<name>。
func Src(pkg, name string) string {
	if pkg == "" {
		return "/src/" + name
	}
	return "/src/" + pkg + "/" + name
}

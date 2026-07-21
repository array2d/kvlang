package keytree

// 路径与成员分隔符统一管理。所有构造 KV 路径、解析限定名、成员访问的地方
// 均须使用这些常量，禁止硬编码 "." 等裸字符串。

const (
	// FuncPathSep 包名与函数名的分隔符。目前为 "."，后期可能变为 "::" 等。
	// 用法：/lib/<pkg><FuncPathSep><name>
	FuncPathSep = "."

	// MemberSep 成员访问分隔符。struct/dict 成员键通过此符附着在基路径上。
	// 与 "/" 结构分隔正交：X/a 是结构子节点，X<MemberSep>a 是同层平坦成员键。
	MemberSep = "."

	// ReservedPrefix 引擎保留字段前缀。kvlang 标识符不能以此开头，用户代码无法写入。
	ReservedPrefix = "."

	// SrcExt 函数源码文件后缀。
	SrcExt = ".src"
)

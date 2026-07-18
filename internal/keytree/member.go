package keytree

// ── 成员键 ────────────────────────────────────────────────────────
//
// 成员键用 "." 分隔（todo-009），与 "/" 结构分隔正交，三种键形态一眼判型：
//
//   X/名   结构（帧、指令槽、label 块）
//   X.名   用户数据成员（键族，同层平坦键）
//   X/.名  系统变量（影子元数据）
//
// struct ≡ dict ≡ 键族：obj 无需作为容器节点存在，obj.a、obj.b 是同层平坦键；
// 嵌套 a.b.c 只是更长的键名，零树深成本——成员表达式即指针。

// Member 返回 base 的成员键：base + "." + name。
func Member(base, name string) string { return base + "." + name }

package keytree

// Member 返回 base 的成员键：base + MemberSep + name。
// struct ≡ dict ≡ 键族，obj.a、obj.b 是同层平坦键。
func Member(base, name string) string { return base + MemberSep + name }

package kvspace

import (
	"testing"
)

// ── resolveWith 单元测试（纯函数，无 Redis）──────────────────────
//
// 存储约定：kv.Set(linkpath, "->"+target)
//   checkLink 读到 "->" 前缀后剥除前缀再存入 links 缓存。
//   resolveWith 的 links 参数直接存 target（前缀已剥除）。
//
// links 语义：
//   非空值 → 该路径是链接，值为 target
//   空字符串 → 否定缓存（已确认非链接，不再 GET Redis）
//   key 不存在 → 未知，checkLink 会 lazy GET Redis

func TestResolveWith_NoLink(t *testing.T) {
	got := resolveWith("/func/pkg/add", map[string]string{})
	if got != "/func/pkg/add" {
		t.Errorf("expected unchanged, got %q", got)
	}
}

func TestResolveWith_ExactMatch(t *testing.T) {
	got := resolveWith("/nodeB", map[string]string{"/nodeB": "/nodeA"})
	if got != "/nodeA" {
		t.Errorf("expected /nodeA, got %q", got)
	}
}

func TestResolveWith_PrefixMatch(t *testing.T) {
	got := resolveWith("/nodeB/foo/bar", map[string]string{"/nodeB": "/nodeA"})
	if got != "/nodeA/foo/bar" {
		t.Errorf("expected /nodeA/foo/bar, got %q", got)
	}
}

func TestResolveWith_NoPrefixFalsePositive(t *testing.T) {
	// /nodeBextra 不应匹配 /nodeB（必须在 '/' 边界处）
	got := resolveWith("/nodeBextra/x", map[string]string{"/nodeB": "/nodeA"})
	if got != "/nodeBextra/x" {
		t.Errorf("expected unchanged, got %q", got)
	}
}

func TestResolveWith_Chain(t *testing.T) {
	// /c → /b → /a，三级链式
	got := resolveWith("/c/foo", map[string]string{"/c": "/b", "/b": "/a"})
	if got != "/a/foo" {
		t.Errorf("expected /a/foo, got %q", got)
	}
}

func TestResolveWith_ShortestPrefixFirst(t *testing.T) {
	// resolve 从短前缀到长前缀扫描：/func 先于 /func/builtin 匹配
	got := resolveWith("/func/builtin/add", map[string]string{"/func": "/f"})
	if got != "/f/builtin/add" {
		t.Errorf("expected /f/builtin/add, got %q", got)
	}
}

func TestResolveWith_Cycle(t *testing.T) {
	// 环：/a → /b → /a，超过 maxHops(40) 后不死循环
	got := resolveWith("/a/x", map[string]string{"/a": "/b", "/b": "/a"})
	_ = got // 只验证不 panic
}

func TestResolveWith_PathSuffix_Preserved(t *testing.T) {
	// kvlang 核心场景：vthread 栈帧链接到 /func/ 指令路径
	got := resolveWith("/vthread/t1/frame0/[3,-2]",
		map[string]string{"/vthread/t1/frame0": "/func/pkg/add"})
	if got != "/func/pkg/add/[3,-2]" {
		t.Errorf("expected /func/pkg/add/[3,-2], got %q", got)
	}
}

func TestResolveWith_RootLink(t *testing.T) {
	links := map[string]string{"/alias": "/real"}
	cases := []struct{ in, want string }{
		{"/alias", "/real"},
		{"/alias/x", "/real/x"},
		{"/alias/x/y/z", "/real/x/y/z"},
		{"/other", "/other"}, // 非链接路径不变
	}
	for _, c := range cases {
		got := resolveWith(c.in, links)
		if got != c.want {
			t.Errorf("resolve(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestResolveWith_NegativeCache(t *testing.T) {
	// 空值 = 否定缓存，resolveWith 应跳过（不当作链接）
	links := map[string]string{
		"/nodeB": "",       // 否定缓存
		"/nodeC": "/nodeA", // 正向链接
	}
	if got := resolveWith("/nodeB/x", links); got != "/nodeB/x" {
		t.Errorf("negative cache: expected unchanged, got %q", got)
	}
	if got := resolveWith("/nodeC/x", links); got != "/nodeA/x" {
		t.Errorf("positive cache: expected /nodeA/x, got %q", got)
	}
}

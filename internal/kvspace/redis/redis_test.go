package redis

import (
	"testing"

	"kvlang/internal/kvspace"
)

// ResolveCore 单元测试（纯函数，无 Redis）
// lookup 直接读 map：非空 = 链接 target；"" = 否定缓存；key 不存在 = 等价于非链接

func testLookup(links map[string]string) func(string) string {
	return func(p string) string { return links[p] }
}

func TestResolveCore_NoLink(t *testing.T) {
	got := kvspace.ResolveCore("/func/pkg/add", testLookup(nil))
	if got != "/func/pkg/add" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_ExactMatch(t *testing.T) {
	got := kvspace.ResolveCore("/nodeB", testLookup(map[string]string{"/nodeB": "/nodeA"}))
	if got != "/nodeA" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_PrefixMatch(t *testing.T) {
	got := kvspace.ResolveCore("/nodeB/foo/bar", testLookup(map[string]string{"/nodeB": "/nodeA"}))
	if got != "/nodeA/foo/bar" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_NoPrefixFalsePositive(t *testing.T) {
	// /nodeBextra 不应匹配 /nodeB（必须在 '/' 边界）
	got := kvspace.ResolveCore("/nodeBextra/x", testLookup(map[string]string{"/nodeB": "/nodeA"}))
	if got != "/nodeBextra/x" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_Chain(t *testing.T) {
	got := kvspace.ResolveCore("/c/foo", testLookup(map[string]string{"/c": "/b", "/b": "/a"}))
	if got != "/a/foo" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_ShortestPrefixFirst(t *testing.T) {
	got := kvspace.ResolveCore("/func/builtin/add", testLookup(map[string]string{"/func": "/f"}))
	if got != "/f/builtin/add" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_Cycle(t *testing.T) {
	// 不应死循环，超过 40 跳后返回
	got := kvspace.ResolveCore("/a/x", testLookup(map[string]string{"/a": "/b", "/b": "/a"}))
	_ = got
}

func TestResolveCore_PathSuffix_Preserved(t *testing.T) {
	got := kvspace.ResolveCore("/frame/t1/frame0/[3,-2]",
		testLookup(map[string]string{"/frame/t1/frame0": "/func/pkg/add"}))
	if got != "/func/pkg/add/[3,-2]" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_RootLink(t *testing.T) {
	lk := testLookup(map[string]string{"/alias": "/real"})
	cases := []struct{ in, want string }{
		{"/alias", "/real"},
		{"/alias/x", "/real/x"},
		{"/alias/x/y/z", "/real/x/y/z"},
		{"/other", "/other"},
	}
	for _, c := range cases {
		if got := kvspace.ResolveCore(c.in, lk); got != c.want {
			t.Errorf("ResolveCore(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestResolveCore_NegativeCache(t *testing.T) {
	lk := testLookup(map[string]string{
		"/nodeB": "",       // 否定缓存：已确认非链接
		"/nodeC": "/nodeA", // 正向链接
	})
	if got := kvspace.ResolveCore("/nodeB/x", lk); got != "/nodeB/x" {
		t.Errorf("negative: got %q", got)
	}
	if got := kvspace.ResolveCore("/nodeC/x", lk); got != "/nodeA/x" {
		t.Errorf("positive: got %q", got)
	}
}

// TestResolveCore_FnLink 模拟 VM 帧链接（/_fn 是唯一实际使用的链接形式）
func TestResolveCore_FnLink(t *testing.T) {
	lk := testLookup(map[string]string{
		"/vthread/42/[3,0]/_fn": "/func/main/add",
	})
	cases := []struct{ in, want string }{
		{"/vthread/42/[3,0]/_fn/[0,0]", "/func/main/add/[0,0]"},
		{"/vthread/42/[3,0]/_fn/[2,-1]", "/func/main/add/[2,-1]"},
		{"/vthread/42/[3,0]/_fn/[5,1]", "/func/main/add/[5,1]"},
		{"/func/main/add/[0,0]", "/func/main/add/[0,0]"}, // 已解析路径不变
	}
	for _, c := range cases {
		if got := kvspace.ResolveCore(c.in, lk); got != c.want {
			t.Errorf("ResolveCore(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

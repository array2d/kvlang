package kvspace

import "testing"

// resolveCore 单元测试（纯函数，无 Redis）
// lookup 直接读 map：非空 = 链接 target；"" = 否定缓存；不存在 = 非链接

func lookup(links map[string]string) func(string) string {
	return func(p string) string { return links[p] }
}

func TestResolveCore_NoLink(t *testing.T) {
	got := resolveCore("/func/pkg/add", lookup(nil))
	if got != "/func/pkg/add" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_ExactMatch(t *testing.T) {
	got := resolveCore("/nodeB", lookup(map[string]string{"/nodeB": "/nodeA"}))
	if got != "/nodeA" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_PrefixMatch(t *testing.T) {
	got := resolveCore("/nodeB/foo/bar", lookup(map[string]string{"/nodeB": "/nodeA"}))
	if got != "/nodeA/foo/bar" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_NoPrefixFalsePositive(t *testing.T) {
	// /nodeBextra 不应匹配 /nodeB（必须在 '/' 边界）
	got := resolveCore("/nodeBextra/x", lookup(map[string]string{"/nodeB": "/nodeA"}))
	if got != "/nodeBextra/x" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_Chain(t *testing.T) {
	got := resolveCore("/c/foo", lookup(map[string]string{"/c": "/b", "/b": "/a"}))
	if got != "/a/foo" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_ShortestPrefixFirst(t *testing.T) {
	got := resolveCore("/func/builtin/add", lookup(map[string]string{"/func": "/f"}))
	if got != "/f/builtin/add" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_Cycle(t *testing.T) {
	// 不应死循环，超过 40 跳后返回
	got := resolveCore("/a/x", lookup(map[string]string{"/a": "/b", "/b": "/a"}))
	_ = got
}

func TestResolveCore_PathSuffix_Preserved(t *testing.T) {
	got := resolveCore("/frame/t1/frame0/[3,-2]",
		lookup(map[string]string{"/frame/t1/frame0": "/func/pkg/add"}))
	if got != "/func/pkg/add/[3,-2]" {
		t.Errorf("got %q", got)
	}
}

func TestResolveCore_RootLink(t *testing.T) {
	lk := lookup(map[string]string{"/alias": "/real"})
	cases := []struct{ in, want string }{
		{"/alias", "/real"},
		{"/alias/x", "/real/x"},
		{"/alias/x/y/z", "/real/x/y/z"},
		{"/other", "/other"},
	}
	for _, c := range cases {
		if got := resolveCore(c.in, lk); got != c.want {
			t.Errorf("resolveCore(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestResolveCore_NegativeCache(t *testing.T) {
	lk := lookup(map[string]string{
		"/nodeB": "",       // 否定缓存：不是链接
		"/nodeC": "/nodeA", // 正向链接
	})
	if got := resolveCore("/nodeB/x", lk); got != "/nodeB/x" {
		t.Errorf("negative: got %q", got)
	}
	if got := resolveCore("/nodeC/x", lk); got != "/nodeA/x" {
		t.Errorf("positive: got %q", got)
	}
}

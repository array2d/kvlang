package json

import (
	"encoding/json"
	"fmt"
	"reflect"
	"strconv"
	"strings"
	"testing"
	"unsafe"
)

// rt 往返一次并返回输出 JSON 与 write 错误。
func rt(c unsafe.Pointer, root, in string) (string, error) {
	v, err := fromJSON([]byte(in))
	if err != nil {
		return "", err
	}
	if err := write(c, root, v); err != nil {
		return "", err
	}
	out, _ := json.Marshal(build(c, root))
	return string(out), nil
}

func check(t *testing.T, c unsafe.Pointer, root, name, in string) bool {
	t.Helper()
	out, err := rt(c, root, in)
	if err != nil {
		t.Errorf("[ERR ] %s: %v\n  in : %s", name, err, trunc(in))
		return false
	}
	if !reflect.DeepEqual(canonical(in), canonical(out)) {
		t.Errorf("[FAIL] %s\n  in : %s\n  out: %s", name, trunc(in), trunc(out))
		return false
	}
	return true
}

func trunc(s string) string {
	if len(s) > 300 {
		return s[:300] + fmt.Sprintf("...(%dB)", len(s))
	}
	return s
}

// ── A. 深嵌套 ────────────────────────────────────────────────────────

func TestDeepNesting(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	for _, depth := range []int{5, 10, 20, 50} {
		obj := "1"
		for i := 0; i < depth; i++ {
			obj = `{"k":` + obj + `}`
		}
		check(t, c, "/cx/deepobj"+strconv.Itoa(depth), fmt.Sprintf("obj-depth-%d", depth), `{"r":`+obj+`}`)

		arr := "1"
		for i := 0; i < depth; i++ {
			arr = `[` + arr + `]`
		}
		check(t, c, "/cx/deeparr"+strconv.Itoa(depth), fmt.Sprintf("arr-depth-%d", depth), `{"r":`+arr+`}`)

		alt := "1"
		for i := 0; i < depth; i++ {
			if i%2 == 0 {
				alt = `[` + alt + `]`
			} else {
				alt = `{"k":` + alt + `}`
			}
		}
		check(t, c, "/cx/deepalt"+strconv.Itoa(depth), fmt.Sprintf("alt-depth-%d", depth), `{"r":`+alt+`}`)
	}
}

// ── B. 宽容器 ────────────────────────────────────────────────────────

func TestWideContainers(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	for _, n := range []int{10, 100, 1000} {
		elems := make([]string, n)
		for i := range elems {
			elems[i] = strconv.Itoa(i)
		}
		check(t, c, "/cx/wideal"+strconv.Itoa(n), fmt.Sprintf("array-%d", n),
			`{"a":[`+strings.Join(elems, ",")+`]}`)

		members := make([]string, n)
		for i := range members {
			members[i] = fmt.Sprintf(`"k%d":%d`, i, i)
		}
		check(t, c, "/cx/wideobj"+strconv.Itoa(n), fmt.Sprintf("object-%d", n),
			`{`+strings.Join(members, ",")+`}`)
	}
}

// ── C. 数值边界 ──────────────────────────────────────────────────────

func TestNumericEdges(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"int64-max", `{"v":9223372036854775807}`},
		{"int64-min", `{"v":-9223372036854775808}`},
		{"zero", `{"v":0}`},
		{"neg-zero-float", `{"v":-0.0}`},
		{"float-tiny", `{"v":5e-324}`},
		{"float-huge", `{"v":1.7976931348623157e308}`},
		{"float-precision", `{"v":0.1}`},
		{"float-long", `{"v":3.141592653589793}`},
		{"sci-notation", `{"v":1.5e-10}`},
		{"int-as-float", `{"v":1.0}`},
		{"big-int-overflow", `{"v":123456789012345678901234567890}`},
		{"neg-int", `{"v":-42}`},
		{"mixed-nums", `{"a":[0,-1,1.5,1e10,-2.5e-5]}`},
	}
	for _, cc := range cases {
		out, err := rt(c, "/cx/num-"+cc.name, cc.in)
		eq := reflect.DeepEqual(canonical(cc.in), canonical(out))
		status := "OK  "
		if err != nil {
			status = "ERR "
		} else if !eq {
			status = "DIFF"
		}
		fmt.Printf("[%s] %-18s in: %-40s out: %s (err=%v)\n", status, cc.name, cc.in, out, err)
	}
}

// ── D. 字符串值边界 ──────────────────────────────────────────────────

func TestStringEdges(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"empty", `{"v":""}`},
		{"ascii", `{"v":"hello world"}`},
		{"cjk", `{"v":"中文日本語한국어"}`},
		{"emoji", `{"v":"🚀🎉👨‍👩‍👧‍👦"}`},
		{"surrogate-pair", `{"v":"😀"}`},
		{"escapes", `{"v":"tab\there\nnewline\r\"quote\"\\backslash"}`},
		{"nul-byte", "{\"v\":\"before\\u0000after\"}"},
		{"ctrl-chars", "{\"v\":\"\\u0001\\u001f\"}"},
		{"slash-dot", `{"v":"a/b.c·d[0]"}`},
		{"runtime-sep", `{"v":"x‥y"}`},
		{"long-10k", `{"v":"` + strings.Repeat("x", 10000) + `"}`},
		{"json-in-string", `{"v":"{\"nested\":[1,2]}"}`},
		{"rtl", `{"v":"مرحبا שלום"}`},
	}
	for _, cc := range cases {
		out, err := rt(c, "/cx/str-"+cc.name, cc.in)
		eq := reflect.DeepEqual(canonical(cc.in), canonical(out))
		status := "OK  "
		if err != nil {
			status = "ERR "
		} else if !eq {
			status = "DIFF"
		}
		fmt.Printf("[%s] %-16s in: %-50s out: %s\n", status, cc.name, trunc(cc.in), trunc(out))
	}
}

// ── E. key 边界 ──────────────────────────────────────────────────────

func TestKeyEdges(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct {
		name, in string
		wantRej  bool
	}{
		{"unicode-key", `{"中文键":1}`, false},
		{"emoji-key", `{"🔑":1}`, false},
		{"dot-key", `{"a.b.c":1}`, false},
		{"decimal-key", `{"3.14":1}`, false},
		{"numeric-key", `{"0":1,"10":2,"2":3}`, false},
		{"space-key", `{"a b":1}`, false},
		{"dash-underscore", `{"a-b_c":1}`, false},
		{"symbol-key", `{"$ref":1,"@type":2,"#id":3}`, false},
		{"brace-key", `{"{}":1}`, false},
		{"long-key", `{"` + strings.Repeat("k", 500) + `":1}`, false},
		{"midpoint-key", `{"a·b":1}`, true},
		{"bracket-key", `{"[0]":1}`, true},
		{"slash-key", `{"a/b":1}`, true},
		{"runtime-sep-key", `{"a‥b":1}`, true},
		{"empty-key", `{"":1}`, true},
		{"tab-key", `{"a\tb":1}`, true},
	}
	for _, cc := range cases {
		out, err := rt(c, "/cx/key-"+cc.name, cc.in)
		rejected := err != nil
		eq := err == nil && reflect.DeepEqual(canonical(cc.in), canonical(out))
		status := "OK  "
		switch {
		case cc.wantRej && rejected:
			status = "REJ✓"
		case cc.wantRej && !rejected:
			status = "LEAK" // 该拒绝却接受了
		case !cc.wantRej && rejected:
			status = "ERR "
		case !eq:
			status = "DIFF"
		}
		fmt.Printf("[%s] %-16s in: %-45s out: %s (err=%v)\n", status, cc.name, trunc(cc.in), trunc(out), err)
	}
}

// ── F. 空容器组合 ────────────────────────────────────────────────────

func TestEmptyContainers(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"empty-root", `{}`},
		{"empty-obj-val", `{"a":{}}`},
		{"empty-arr-val", `{"a":[]}`},
		{"arr-of-empty-arr", `{"a":[[],[],[]]}`},
		{"arr-of-empty-obj", `{"a":[{},{},{}]}`},
		{"nested-empty", `{"a":{"b":{"c":{}}}}`},
		{"mixed-empty", `{"a":[],"b":{},"c":[{}],"d":{"e":[]}}`},
		{"empty-then-value", `{"a":{},"b":1}`},
	}
	for _, cc := range cases {
		check(t, c, "/cx/empty-"+cc.name, cc.name, cc.in)
	}
}

// ── G. null 位置 ─────────────────────────────────────────────────────

func TestNullPositions(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"null-member", `{"a":null}`},
		{"all-null-obj", `{"a":null,"b":null,"c":null}`},
		{"null-in-array", `{"a":[null,1,null]}`},
		{"all-null-array", `{"a":[null,null,null]}`},
		{"null-nested", `{"a":{"b":[null,{"c":null}]}}`},
		{"null-vs-empty-str", `{"a":null,"b":""}`},
		{"null-vs-zero", `{"a":null,"b":0,"c":false}`},
	}
	for _, cc := range cases {
		check(t, c, "/cx/null-"+cc.name, cc.name, cc.in)
	}
}

// ── H. 真实世界结构 ──────────────────────────────────────────────────

const geoJSON = `{"type":"FeatureCollection","features":[{"type":"Feature","geometry":{"type":"Polygon","coordinates":[[[116.3,39.9],[116.4,39.9],[116.4,40.0],[116.3,39.9]]]},"properties":{"name":"区域A","area":12.5,"tags":["park","urban"],"meta":null}},{"type":"Feature","geometry":{"type":"Point","coordinates":[121.47,31.23]},"properties":{"name":"点B","area":0,"tags":[],"meta":{"src":"gps","acc":0.001}}}]}`

const pkgJSON = `{"name":"@scope/pkg","version":"1.2.3-beta.1","private":false,"scripts":{"build":"tsc -p .","test":"jest --coverage"},"dependencies":{"react":"^18.2.0","lodash":"~4.17.21"},"devDependencies":{"typescript":"5.0.0"},"exports":{".":{"import":"./dist/index.mjs","require":"./dist/index.cjs"}},"files":["dist","README.md"],"engines":{"node":">=18"},"keywords":[]}`

const apiJSON = `{"code":0,"msg":"ok","data":{"total":2,"page":1,"items":[{"id":1001,"name":"张三","score":95.5,"tags":["vip","new"],"profile":{"age":28,"addr":{"city":"北京","zip":"100000"},"phone":null}},{"id":1002,"name":"李四","score":88,"tags":[],"profile":{"age":31,"addr":{"city":"上海","zip":"200000"},"phone":"138-0000-0000"}}],"cursor":null},"ts":1735689600000}`

const configJSON = `{"server":{"host":"0.0.0.0","port":8080,"tls":{"enabled":true,"cert":"/etc/ssl/cert.pem","ciphers":["TLS_AES_128_GCM_SHA256","TLS_AES_256_GCM_SHA384"]}},"logging":{"level":"info","outputs":[{"type":"file","path":"/var/log/app.log","rotate":{"size":"100MB","keep":7}},{"type":"stdout"}]},"features":{"a":true,"b":false,"c":null},"limits":{"rps":1000,"burst":2000,"timeout":30.5}}`

func TestRealWorldShapes(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"geojson", geoJSON},
		{"package-json", pkgJSON},
		{"api-response", apiJSON},
		{"config", configJSON},
	}
	for _, cc := range cases {
		check(t, c, "/cx/real-"+cc.name, cc.name, cc.in)
	}
}

// ── I. 异构与混合 ────────────────────────────────────────────────────

func TestHeterogeneous(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"all-types-array", `{"a":[null,true,false,0,-1,1.5,"s",[],{},[1],{"k":1}]}`},
		{"array-of-arrays-mixed", `{"a":[[1,"x"],[true,null],[{"k":[1,2]}]]}`},
		{"matrix-3d", `{"m":[[[1,2],[3,4]],[[5,6],[7,8]]]}`},
		{"ragged", `{"m":[[1],[1,2],[1,2,3]]}`},
		{"obj-array-alternating", `{"a":[{"b":[{"c":[{"d":1}]}]}]}`},
		{"same-key-diff-depth", `{"k":1,"a":{"k":2,"b":{"k":3}}}`},
		{"numeric-string-vs-num", `{"a":["1",1,"1.5",1.5,"true",true]}`},
	}
	for _, cc := range cases {
		check(t, c, "/cx/het-"+cc.name, cc.name, cc.in)
	}
}

// ── J. 顶层非 object ─────────────────────────────────────────────────

func TestTopLevelNonObject(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []string{`[1,2,3]`, `42`, `"str"`, `null`, `true`, `[]`, `[{"a":1}]`}
	for i, in := range cases {
		out, err := rt(c, "/cx/top"+strconv.Itoa(i), in)
		fmt.Printf("[%-4s] top-level %-12s out: %s err: %v\n",
			map[bool]string{true: "ERR", false: "OK"}[err != nil], in, out, err)
	}
}

// ── K. 覆盖写（同 root 重写，旧成员是否残留）─────────────────────────

func TestOverwrite(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, first, second string }{
		{"shrink-obj", `{"a":1,"b":2,"c":3}`, `{"a":9}`},
		{"shrink-array", `{"a":[1,2,3,4,5]}`, `{"a":[1]}`},
		{"obj-to-scalar", `{"a":{"x":1}}`, `{"a":1}`},
		{"scalar-to-obj", `{"a":1}`, `{"a":{"x":1}}`},
		{"array-to-obj", `{"a":[1,2]}`, `{"a":{"x":1}}`},
		{"null-to-value", `{"a":null}`, `{"a":1}`},
		{"value-to-null", `{"a":1}`, `{"a":null}`},
		{"empty-after-full", `{"a":1,"b":2}`, `{}`},
	}
	for _, cc := range cases {
		root := "/cx/ow-" + cc.name
		if _, err := rt(c, root, cc.first); err != nil {
			t.Errorf("%s: first write err %v", cc.name, err)
			continue
		}
		out, err := rt(c, root, cc.second)
		eq := err == nil && reflect.DeepEqual(canonical(cc.second), canonical(out))
		status := "OK  "
		if err != nil {
			status = "ERR "
		} else if !eq {
			status = "STALE" // 旧数据残留
		}
		fmt.Printf("[%s] %-18s %s → %s\n       got: %s\n", status, cc.name, cc.first, cc.second, out)
	}
}

// ── L. 二次往返的 kind 稳定性（json→kv→json→kv 后落盘 kind 是否漂移）──

func TestKindStability(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in, path string }{
		{"float-integral", `{"v":1.0}`, "·v"},
		{"float-1e10", `{"v":1e10}`, "·v"},
		{"float-frac", `{"v":1.5}`, "·v"},
		{"int", `{"v":7}`, "·v"},
		{"neg-zero", `{"v":-0.0}`, "·v"},
		{"bool", `{"v":true}`, "·v"},
		{"str", `{"v":"x"}`, "·v"},
	}
	for _, cc := range cases {
		r1 := "/ks/a-" + cc.name
		out1, _ := rt(c, r1, cc.in)
		k1, _, _ := parseTLV(getTLV(c, r1+cc.path))
		r2 := "/ks/b-" + cc.name
		out2, _ := rt(c, r2, out1)
		k2, _, _ := parseTLV(getTLV(c, r2+cc.path))
		status := "OK  "
		if k1 != k2 {
			status = "DRIFT"
		}
		fmt.Printf("[%s] %-15s %s → kind=%-9s → %s → kind=%-9s (out2=%s)\n",
			status, cc.name, cc.in, k1, out1, k2, out2)
	}
}

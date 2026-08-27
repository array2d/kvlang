package json

import (
	"encoding/json"
	"testing"
)

// TestNativeDataTo：json·to 对 kvlang 原生数据（/ 目录树 + compact ndarray + 散 key 数组）序列化。
func TestNativeDataTo(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)

	delTree(c, "/d")
	delTree(c, "/b")
	mkindex(c, "/d/")
	setTLV(c, "/d/p", constructTLV("int64", u64(1), 1))
	setTLV(c, "/d/q", constructTLV("int64", u64(2), 1))

	raw := append(append(append([]byte{}, u64(1)...), u64(2)...), u64(3)...)
	setTLV(c, "/b", constructTLV("int64", raw, 3))

	if got, _ := json.Marshal(build(c, "/d")); string(got) != `{"p":1,"q":2}` {
		t.Errorf("dir got %s", got)
	}
	if got, _ := json.Marshal(build(c, "/b")); string(got) != `[1,2,3]` {
		t.Errorf("compact got %s", got)
	}

	// 散 key 数组成员目录（/d/scat·）作为 / 目录的子节点，子名带 · 需 strip。
	if err := write(c, "/d/scat", []interface{}{json.Number("10"), json.Number("20"), json.Number("30")}); err != nil {
		t.Fatal(err)
	}
	if got, _ := json.Marshal(build(c, "/d")); string(got) != `{"p":1,"q":2,"scat":[10,20,30]}` {
		t.Errorf("dir with scatter got %s", got)
	}
}

package json

import (
	"encoding/json"
	"testing"
)

func TestNativeDataTo(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)

	delTree(c, "/d")
	delTree(c, "/b")
	mkindex(c, "/d/")
	setXValue(c, "/d/p", constructXValue("int64", u64(1), 1))
	setXValue(c, "/d/q", constructXValue("int64", u64(2), 1))

	raw := append(append(append([]byte{}, u64(1)...), u64(2)...), u64(3)...)
	setXValue(c, "/b", constructXValue("int64", raw, 3))

	if got, _ := json.Marshal(build(c, "/d")); string(got) != `{"p":1,"q":2}` {
		t.Errorf("dir got %s", got)
	}
	if got, _ := json.Marshal(build(c, "/b")); string(got) != `[1,2,3]` {
		t.Errorf("compact got %s", got)
	}

	if err := write(c, "/d/scat", []interface{}{json.Number("10"), json.Number("20"), json.Number("30")}); err != nil {
		t.Fatal(err)
	}
	if got, _ := json.Marshal(build(c, "/d")); string(got) != `{"p":1,"q":2,"scat":[10,20,30]}` {
		t.Errorf("dir with scatter got %s", got)
	}
}

func TestLocalContainer(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)

	if err := write(c, "/rt/profile", map[string]any{
		"name": "alice", "age": json.Number("42"),
		"active": true, "score": json.Number("3.14"),
		"nums": []interface{}{json.Number("1"), json.Number("2"), json.Number("3")},
	}); err != nil {
		t.Fatal(err)
	}
	if got, _ := json.Marshal(build(c, "/rt/profile")); string(got) != `{"active":true,"age":42,"name":"alice","nums":[1,2,3],"score":3.14}` {
		t.Errorf("obj got %s", got)
	}

	if err := write(c, "/rt/map", []interface{}{json.Number("10"), json.Number("20"), json.Number("30")}); err != nil {
		t.Fatal(err)
	}
	if got, _ := json.Marshal(build(c, "/rt/map")); string(got) != `[10,20,30]` {
		t.Errorf("map got %s", got)
	}

	if err := write(c, "/rt/tags", []interface{}{"a", "b", "c"}); err != nil {
		t.Fatal(err)
	}
	if got, _ := json.Marshal(build(c, "/rt/tags")); string(got) != `["a","b","c"]` {
		t.Errorf("tags got %s", got)
	}
}

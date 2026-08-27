package json

import (
	"encoding/json"
	"fmt"
	"os"
	"testing"
	"unsafe"
)

func rtConn(t *testing.T) unsafe.Pointer {
	dsn := os.Getenv("KVSPACE")
	if dsn == "" {
		dsn = "redis://127.0.0.1:6379"
	}
	c := connect(dsn)
	if c == nil {
		t.Fatalf("connect failed: %s", dsn)
	}
	return c
}

func roundTrip(c unsafe.Pointer, root, input string) string {
	v, err := fromJSON([]byte(input))
	if err != nil {
		return "<fromJSON error>"
	}
	_ = write(c, root, v)
	out, _ := json.Marshal(build(c, root))
	return string(out)
}

func TestRepresentative(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []struct{ name, in string }{
		{"scalar-obj", `{"a":1,"b":true,"c":"x","d":3.14,"e":null}`},
		{"nested-obj", `{"a":{"b":{"c":1}},"d":2}`},
		{"obj-array", `{"list":[{"x":1},{"y":2}]}`},
		{"str-array", `{"list":["a","b","c"]}`},
		{"mixed-array", `{"list":[1,"a",true,null,3.14]}`},
		{"nested-array", `{"m":[[1,2],[3,4]]}`},
		{"num-array", `{"list":[1,2,3]}`},
		{"empty-obj", `{}`},
		{"empty-array", `{"list":[]}`},
		{"deep", `{"a":{"b":[1,{"c":[true,false]},"z"]}}`},
	}
	for _, cc := range cases {
		got := roundTrip(c, "/rt/"+cc.name, cc.in)
		eq := "OK  "
		if got != cc.in {
			eq = "FAIL"
		}
		fmt.Printf("[%s] %s\n    in : %s\n    out: %s\n", eq, cc.name, cc.in, got)
	}
}

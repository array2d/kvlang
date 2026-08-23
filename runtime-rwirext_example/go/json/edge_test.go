package json

import (
	"encoding/json"
	"fmt"
	"testing"
)

func TestEdgeKeys(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	cases := []string{
		`{"a.b":1}`,              // 点号 key（应拒绝）
		`{"a/b":1}`,              // 斜杠 key（应拒绝）
		`{"a[b]":1}`,             // 方括号 key（应拒绝）
		`{"a\nb":1}`,             // 换行 key（应拒绝）
		`{"0":"zero","1":"one"}`, // 数字字符串 key（合法）
		`{"":1}`,                 // 空 key（应拒绝）
		`{"a":"v.b/c\nx"}`,       // 值含特殊字符（应允许）
	}
	for i, in := range cases {
		root := "/rt/edge" + fmt.Sprint(i)
		err := writeMap(c, root, fromJSON([]byte(in)))
		status := "OK  "
		if err != nil {
			status = "REJ "
		}
		out, _ := json.Marshal(buildMap(c, root))
		fmt.Printf("[%s] in: %s\n       err: %v\n       out: %s\n", status, in, err, out)
	}
}

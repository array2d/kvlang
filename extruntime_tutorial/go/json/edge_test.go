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
		`{"a.b":1}`,              
		`{"a/b":1}`,              
		`{"a[b]":1}`,             
		`{"a·b":1}`,              
		`{"a‥b":1}`,              
		`{"a\nb":1}`,             
		`{"0":"zero","1":"one"}`, 
		`{"":1}`,                 
		`{"a":"v.b/c\nx"}`,       
	}
	for i, in := range cases {
		root := "/rt/edge" + fmt.Sprint(i)
		v, err := fromJSON([]byte(in))
		if err == nil {
			err = write(c, root, v)
		}
		status := "OK  "
		if err != nil {
			status = "REJ "
		}
		out, _ := json.Marshal(build(c, root))
		fmt.Printf("[%s] in: %s\n       err: %v\n       out: %s\n", status, in, err, out)
	}
}

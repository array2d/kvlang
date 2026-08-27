package json

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"reflect"
	"strconv"
	"strings"
	"testing"
)

// genValue 随机生成 JSON 值（用 json.Number 保留数值精度）。
func genValue(r *rand.Rand, depth int) interface{} {
	if depth > 4 {
		return genScalar(r)
	}
	switch r.Intn(8) {
	case 0:
		return nil
	case 1:
		return r.Intn(2) == 0
	case 2:
		return json.Number(strconv.Itoa(r.Intn(2000) - 1000))
	case 3:
		return json.Number(fmt.Sprintf("%.4f", r.Float64()*2000-1000))
	case 4:
		return genString(r)
	case 5: // array
		n := r.Intn(6)
		arr := make([]interface{}, n)
		for i := range arr {
			arr[i] = genValue(r, depth+1)
		}
		return arr
	case 6: // object
		n := r.Intn(6)
		m := map[string]interface{}{}
		for i := 0; i < n; i++ {
			m[genKey(r)] = genValue(r, depth+1)
		}
		return m
	default:
		return genScalar(r)
	}
}

func genScalar(r *rand.Rand) interface{} {
	switch r.Intn(4) {
	case 0:
		return nil
	case 1:
		return r.Intn(2) == 0
	case 2:
		return json.Number(strconv.Itoa(r.Intn(2000) - 1000))
	default:
		return genString(r)
	}
}

func genString(r *rand.Rand) string {
	pool := []string{"", "a", "hello", "中文", "emoji🚀", "line\nbreak", "quote\"x", "back\\slash", "tab\tx", "0"}
	return pool[r.Intn(len(pool))]
}

func genKey(r *rand.Rand) string {
	pool := []string{"a", "b", "c", "name", "x1", "_under", "k-2", "中文键"}
	return pool[r.Intn(len(pool))]
}

// normalize 把 json.Number 转成 int64/float64，供 reflect.DeepEqual 语义比较。
func normalize(v interface{}) interface{} {
	switch t := v.(type) {
	case json.Number:
		if i, err := t.Int64(); err == nil {
			return i
		}
		f, _ := t.Float64()
		return f
	case map[string]interface{}:
		m := map[string]interface{}{}
		for k, vv := range t {
			m[k] = normalize(vv)
		}
		return m
	case []interface{}:
		arr := make([]interface{}, len(t))
		for i, vv := range t {
			arr[i] = normalize(vv)
		}
		return arr
	default:
		return v
	}
}

func canonical(s string) interface{} {
	var v interface{}
	dec := json.NewDecoder(strings.NewReader(s))
	dec.UseNumber()
	if err := dec.Decode(&v); err != nil {
		return nil
	}
	return normalize(v)
}

func TestRandomRoundtrip(t *testing.T) {
	c := rtConn(t)
	defer disconnect(c)
	r := rand.New(rand.NewSource(42))
	fail := 0
	for i := 0; i < 300; i++ {
		v := genValue(r, 0)
		in, _ := json.Marshal(v)
		root := "/rt/rand" + strconv.Itoa(i)
		v, err := fromJSON(in)
		if err != nil {
			continue
		}
		_ = write(c, root, v)
		out, _ := json.Marshal(build(c, root))
		if !reflect.DeepEqual(canonical(string(in)), canonical(string(out))) {
			fail++
			if fail <= 8 {
				fmt.Printf("[FAIL #%d]\n  in : %s\n  out: %s\n", i, in, out)
			}
		}
	}
	fmt.Printf("random roundtrip: 300 cases, %d mismatches\n", fail)
}

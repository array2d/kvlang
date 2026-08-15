// Package json 提供 json.to / json.from 两个 rwir 的 rwirext（外部执行器）。
// json.to(rootkey)：把 rootkey 下的整棵子树递归读成 map[string]any 再 json.Marshal。
// json.from(json)：把 JSON 反序列化为 map[string]any 再递归写回 KV 子树。
// 数组：compact []T（单 key 多元素）与散 key（name<0>..name<N-1>）统一序列化为 JSON 数组。
package json

import (
	"bytes"
	"context"
	"encoding/json"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/rwir"
	"kvlang/rwir/builtin"
)

var ops = []string{"json.to", "json.from"}

// Register 注册 json.to/json.from 两个 rwir 到 /rwir/（kind=rwir），幂等。
func Register(kv kvspace.KVSpace) {
	kv.Set([]kvspace.KVPair{
		{Key: "/rwir/json.to", Val: kvspace.NewRwir(1, 1, "rwir json.to(rootkey:charbyte) -> (dest:[]charbyte)")},
		{Key: "/rwir/json.from", Val: kvspace.NewRwir(1, 1, "rwir json.from(src:[]charbyte) -> (rootkey:charbyte)")},
	})
	for _, op := range ops {
		builtin.RegisterGlobalRwir(op)
	}
}

// Serve 常驻循环：持续处理各 rwir 的 .todo<vid>。
func Serve(kv kvspace.KVSpace) {
	for {
		Register(kv) // 幂等重注册，兜底外部 FLUSHALL 清空 /rwir
		for _, op := range ops {
			serve(kv, op)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func serve(kv kvspace.KVSpace, op string) {
	ctx := context.Background()
	base := "/rwir/" + op
	for _, child := range kv.List(base+"/", false, false) {
		if !strings.HasPrefix(child, ".todo<") {
			continue
		}
		vid := strings.TrimSuffix(strings.TrimPrefix(child, ".todo<"), ">")
		pcID := kvspace.GetOne(kv, base+"/"+child).ValueString()
		pc, id := pcID, ""
		if i := strings.LastIndex(pcID, "|"); i >= 0 {
			pc, id = pcID[:i], pcID[i+1:]
		}

		if op == "json.to" {
			doTo(ctx, kv, pc)
		} else {
			doFrom(ctx, kv, pc)
		}

		kv.Set([]kvspace.KVPair{{Key: base + "/.done<" + vid + ">", Val: kvspace.NewCharByte([]byte(id)...)}})
		kv.Del(base + "/" + child)
	}
}

// doTo 序列化：rootkey 子树 → map[string]any → json.Marshal → 写回写参 dest。
func doTo(ctx context.Context, kv kvspace.KVSpace, pc string) {
	inst, err := rwir.Decode(ctx, kv, keytree.Stack(keytree.FrameRoot(pc)), pc)
	if err != nil || len(inst.Reads) == 0 || len(inst.Writes) == 0 {
		return
	}
	root := rootKey(kv, keytree.FrameRoot(pc), inst.Reads[0])
	data, _ := json.Marshal(buildMap(kv, root))
	writeKey := builtin.ResolveWriteSlot(kv, keytree.FrameRoot(pc), inst.Writes[0].Name)
	kv.Set([]kvspace.KVPair{{Key: writeKey, Val: kvspace.NewCharByte(data...)}})
}

// doFrom 反序列化：JSON → map[string]any → 递归写回 rootkey（写参）子树。
func doFrom(ctx context.Context, kv kvspace.KVSpace, pc string) {
	inst, err := rwir.Decode(ctx, kv, keytree.Stack(keytree.FrameRoot(pc)), pc)
	if err != nil || len(inst.Reads) == 0 || len(inst.Writes) == 0 {
		return
	}
	src := builtin.ResolveReadValue(kv, keytree.FrameRoot(pc), inst.Reads[0])
	root := builtin.ResolveWriteSlot(kv, keytree.FrameRoot(pc), inst.Writes[0].Name)
	writeMap(kv, root, fromJSON([]byte(src.ValueString())))
}

// rootKey 解析读参为 KV 路径：/ 开头直接用，否则取变量的字符串值。
func rootKey(kv kvspace.KVSpace, framePath string, r rwir.Param) string {
	if strings.HasPrefix(r.Name, "/") {
		return r.Name
	}
	return builtin.ResolveReadValue(kv, framePath, r).ValueString()
}

// buildMap 递归把 root 下的子树读成 map[string]any。
// 目录→嵌套 map；散 key 数组（name<0>..name<N-1>）→ JSON 数组；叶子→Go 值。
func buildMap(kv kvspace.KVSpace, root string) map[string]any {
	m := map[string]any{}
	scat := map[string][]int{}
	for _, child := range kv.List(root+"/", false, false) {
		if strings.HasSuffix(child, "/") {
			name := strings.TrimSuffix(child, "/")
			m[name] = buildMap(kv, root+"/"+name)
			continue
		}
		if base, idx, ok := splitArrayName(child); ok {
			scat[base] = append(scat[base], idx)
			continue
		}
		m[child] = toJSONValue(kvspace.GetOne(kv, root+"/"+child))
	}
	for base, idxs := range scat {
		sort.Ints(idxs)
		arr := make([]interface{}, len(idxs))
		for i, idx := range idxs {
			arr[i] = toJSONValue(kvspace.GetOne(kv, root+"/"+base+"<"+strconv.Itoa(idx)+">"))
		}
		m[base] = arr
	}
	return m
}

// writeMap 递归把 map[string]any 写回 root 下的 KV 子树。
// map→目录；[]any→连续数组；其余→叶子。
func writeMap(kv kvspace.KVSpace, root string, m map[string]any) {
	for k, v := range m {
		childPath := root + "/" + k
		switch t := v.(type) {
		case map[string]any:
			kvspace.MkIndexRecursive(kv, childPath+"/")
			writeMap(kv, childPath, t)
		case []interface{}:
			kv.Set([]kvspace.KVPair{{Key: childPath, Val: fromJSONArray(t)}})
		default:
			kv.Set([]kvspace.KVPair{{Key: childPath, Val: fromJSONValue(v)}})
		}
	}
}

// splitArrayName 解析散 key 数组元素名 name<i> → (name, i, ok)。
func splitArrayName(name string) (base string, idx int, ok bool) {
	lt := strings.LastIndex(name, "<")
	if lt <= 0 || !strings.HasSuffix(name, ">") {
		return "", 0, false
	}
	i, err := strconv.Atoi(name[lt+1 : len(name)-1])
	if err != nil {
		return "", 0, false
	}
	return name[:lt], i, true
}

// toJSONValue 把叶子/数组 XValue 转成 Go 值。charbyte→字符串；多元素→[]any。
func toJSONValue(v kvspace.XValue) interface{} {
	if kvspace.IsNone(v) {
		return nil
	}
	n := int(v.ArrayLen())
	if kvspace.IsCharKind(v.Kind()) {
		return v.ValueString()
	}
	if n > 1 && kvspace.ElemSize(v.Kind()) > 0 {
		arr := make([]interface{}, n)
		for i := 0; i < n; i++ {
			arr[i] = elemJSON(v, i)
		}
		return arr
	}
	return elemJSON(v, 0)
}

// elemJSON 按具体类型读取第 idx 个元素（标量或数组元素）为 Go 值。
func elemJSON(v kvspace.XValue, idx int) interface{} {
	switch t := v.(type) {
	case kvspace.Bool:
		return t.At(idx)
	case kvspace.Int8:
		return t.At(idx)
	case kvspace.Int16:
		return t.At(idx)
	case kvspace.Int32:
		return t.At(idx)
	case kvspace.Int64:
		return t.At(idx)
	case kvspace.Uint8:
		return t.At(idx)
	case kvspace.Uint16:
		return t.At(idx)
	case kvspace.Uint32:
		return t.At(idx)
	case kvspace.Uint64:
		return t.At(idx)
	case kvspace.Float32:
		return t.At(idx)
	case kvspace.Float64:
		return t.At(idx)
	default:
		return v.ValueString()
	}
}

// fromJSON 解析 JSON 字节为 map[string]any（UseNumber 保留整数/浮点区分）。
func fromJSON(data []byte) map[string]any {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	var m map[string]any
	if err := dec.Decode(&m); err != nil {
		return nil
	}
	return m
}

// fromJSONArray 把 JSON 数组打包为连续数组 XValue（统一元素类型）。
func fromJSONArray(arr []interface{}) kvspace.XValue {
	if len(arr) == 0 {
		return kvspace.None{}
	}
	switch arr[0].(type) {
	case json.Number:
		ints := make([]int64, len(arr))
		for i, e := range arr {
			n, ok := e.(json.Number)
			if !ok {
				return kvspace.None{}
			}
			iv, err := n.Int64()
			if err != nil {
				floats := make([]float64, len(arr))
				for j, e := range arr {
					n, ok := e.(json.Number)
					if !ok {
						return kvspace.None{}
					}
					f, _ := n.Float64()
					floats[j] = f
				}
				return kvspace.NewFloat64(floats...)
			}
			ints[i] = iv
		}
		return kvspace.NewInt64(ints...)
	case bool:
		bs := make([]bool, len(arr))
		for i, e := range arr {
			b, ok := e.(bool)
			if !ok {
				return kvspace.None{}
			}
			bs[i] = b
		}
		return kvspace.NewBool(bs...)
	default:
		return kvspace.None{}
	}
}

// fromJSONValue 把 JSON 标量值构造为 XValue。
func fromJSONValue(v interface{}) kvspace.XValue {
	switch t := v.(type) {
	case json.Number:
		if i, err := t.Int64(); err == nil {
			return kvspace.NewInt64(i)
		}
		f, _ := t.Float64()
		return kvspace.NewFloat64(f)
	case string:
		return kvspace.NewCharByte([]byte(t)...)
	case bool:
		return kvspace.NewBool(t)
	default:
		return kvspace.None{}
	}
}

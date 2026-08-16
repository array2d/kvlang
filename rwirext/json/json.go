// Package json 是 rwirext 扩展运行时（另一个是 term）。
//
// 一个 rwirext 只需做三件事：
//  1. 用 ext.Ext 声明己方 rwir（opcode + 签名 + 读写参数量）；
//  2. Register/Serve 两行转发（注册签名、常驻监控 .todo、批量执行、交还 PC）；
//  3. exec 按 opcode 分发到具体 handler。
//
// 中央 kvlang runtime 把控制权交给扩展运行时，扩展运行时批量执行己方 rwir
// 直到遇到非己方指令，再把最终 PC 写回 /vthread/<vtid>/pc。livebyte 的 agent
// 扩展照此模板实现即可：声明 op 集合与 exec 分发，其余由框架负责。
package json

import (
	"bytes"
	"context"
	"encoding/json"
	"sort"
	"strconv"
	"strings"

	"github.com/array2d/kvspace-go"
	"kvlang/keytree"
	"kvlang/rwir"
	"kvlang/rwir/builtin"
	"kvlang/rwir/ext"
)

// rt 声明 json 扩展运行时：json.to / json.from 两个 rwir。
var rt = ext.Ext{
	Ops: []ext.Op{
		{Name: "json.to", Sig: "rwir json.to(rootkey:charbyte) -> (dest:[]charbyte)", Nr: 1, Nw: 1},
		{Name: "json.from", Sig: "rwir json.from(src:[]charbyte) -> (rootkey:charbyte)", Nr: 1, Nw: 1},
	},
	Exec: exec,
}

func Register(kv kvspace.KVSpace) { rt.Register(kv) }
func Serve(kv kvspace.KVSpace)    { rt.Serve(kv) }

func exec(_ context.Context, kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	if inst.Opcode == "json.to" {
		doTo(kv, pc, inst)
	} else {
		doFrom(kv, pc, inst)
	}
}

// doTo 序列化：rootkey 子树 → map[string]any → json.Marshal → 写回写参 dest。
func doTo(kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	if len(inst.Reads) == 0 || len(inst.Writes) == 0 {
		return
	}
	root := rootKey(kv, keytree.FrameRoot(pc), inst.Reads[0])
	data, _ := json.Marshal(buildMap(kv, root))
	writeKey := builtin.ResolveWriteSlot(kv, keytree.FrameRoot(pc), inst.Writes[0].Name)
	kv.Set([]kvspace.KVPair{{Key: writeKey, Val: kvspace.NewCharByte(data...)}})
}

// doFrom 反序列化：JSON → map[string]any → 递归写回 rootkey（写参）子树。
func doFrom(kv kvspace.KVSpace, pc string, inst *rwir.Rwir) {
	if len(inst.Reads) == 0 || len(inst.Writes) == 0 {
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

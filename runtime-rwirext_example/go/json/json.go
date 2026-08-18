// Package json 是 rwirext 扩展运行时（json.to/json.from）。经 C runtime 的
// rwext_* ABI 与 kvspace 交互（不依赖 kvspace-go）。独立进程常驻 serve。
package json

/*
#cgo CFLAGS: -I${SRCDIR}/../../../runtime/include
#cgo LDFLAGS: -L${SRCDIR}/../../../bin -lkvlang_runtime -Wl,-rpath,${SRCDIR}/../../../bin
#cgo LDFLAGS: -Wl,-rpath-link,${SRCDIR}/../../../../kvspace/build
#cgo LDFLAGS: -Wl,-rpath-link,${SRCDIR}/../../../../blockmalloc/build
#cgo LDFLAGS: -Wl,-rpath-link,${SRCDIR}/../../../../slotsboxmalloc/build
#cgo LDFLAGS: -Wl,-rpath-link,${SRCDIR}/../../../../kvspace-durable/target/release
#include "kvlang_rwext.h"
#include <stdlib.h>
*/
import "C"

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"math"
	"sort"
	"strconv"
	"strings"
	"time"
	"unsafe"
)

// ── cgo 封装 ────────────────────────────────────────────────────────

func cstr(s string) *C.char { return C.CString(s) }

func gostr(s *C.char) string {
	if s == nil {
		return ""
	}
	defer C.free(unsafe.Pointer(s))
	return C.GoString(s)
}

func list(c *C.rwext_conn, prefix string) []string {
	cp := cstr(prefix)
	defer C.free(unsafe.Pointer(cp))
	s := C.rwext_list(c, cp)
	if s == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(s))
	str := C.GoString(s)
	if str == "" {
		return nil
	}
	return strings.Split(str, "\n")
}

func get(c *C.rwext_conn, key string) string {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	return gostr(C.rwext_get(c, ck))
}

func getTLV(c *C.rwext_conn, key string) []byte {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	var out *C.uint8_t
	var outLen C.uint32_t
	C.rwext_get_tlv(c, ck, &out, &outLen)
	if out == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(out))
	return C.GoBytes(unsafe.Pointer(out), C.int(outLen))
}

func setTLV(c *C.rwext_conn, key string, tlv []byte) {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	buf := C.CBytes(tlv)
	defer C.free(buf)
	C.rwext_set_tlv(c, ck, (*C.uint8_t)(buf), C.uint32_t(len(tlv)))
}

func setChar(c *C.rwext_conn, key, val string) {
	ck := cstr(key)
	cv := cstr(val)
	defer C.free(unsafe.Pointer(ck))
	defer C.free(unsafe.Pointer(cv))
	C.rwext_set(c, ck, cv)
}

func mkindex(c *C.rwext_conn, path string) {
	cp := cstr(path)
	defer C.free(unsafe.Pointer(cp))
	C.rwext_mkindex(c, cp)
}

func resolveRead(c *C.rwext_conn, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.rwext_resolve_read(c, cp, C.int(idx)))
}

func resolveWrite(c *C.rwext_conn, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.rwext_resolve_write(c, cp, C.int(idx)))
}

func nextPC(pc string) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.rwext_next_pc(cp))
}

func params(c *C.rwext_conn, pc string) []string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return strings.Split(gostr(C.rwext_params(c, cp)), "\n")
}

// ── TLV 编解码（kindexp：kl|kind|ref|arr_flag|ndim|dims|raw_len|raw）───

func u32(v uint32) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, v)
	return b
}

func parseTLV(data []byte) (kind string, raw []byte, arrLen int) {
	if len(data) < 4 {
		return "", nil, 0
	}
	kl := int(data[0])
	kind = string(data[1 : 1+kl])
	o := 1 + kl
	arrFlag := data[o+1]
	ndim := int(data[o+2])
	rawOff := o + 3 + 4*ndim
	if rawOff+4 > len(data) {
		return kind, nil, 1
	}
	rawLen := int(binary.LittleEndian.Uint32(data[rawOff : rawOff+4]))
	raw = data[rawOff+4 : rawOff+4+rawLen]
	if arrFlag == 0 {
		arrLen = 1
	} else {
		arrLen = 1
		for i := 0; i < ndim; i++ {
			arrLen *= int(binary.LittleEndian.Uint32(data[o+3+4*i : o+3+4*i+4]))
		}
	}
	return kind, raw, arrLen
}

func constructTLV(kind string, raw []byte, arrLen int) []byte {
	var b bytes.Buffer
	b.WriteByte(byte(len(kind)))
	b.WriteString(kind)
	b.WriteByte(0) // ref
	if arrLen > 1 {
		b.WriteByte(1) // arr_flag
		b.WriteByte(1) // ndim
		b.Write(u32(uint32(arrLen)))
	} else {
		b.WriteByte(0)
		b.WriteByte(0)
	}
	b.Write(u32(uint32(len(raw))))
	b.Write(raw)
	return b.Bytes()
}

// ── JSON 值 ↔ TLV ───────────────────────────────────────────────────

func readInt(raw []byte) int64 {
	var v int64
	switch len(raw) {
	case 1:
		v = int64(int8(raw[0]))
	case 2:
		v = int64(int16(binary.LittleEndian.Uint16(raw)))
	case 4:
		v = int64(int32(binary.LittleEndian.Uint32(raw)))
	case 8:
		v = int64(binary.LittleEndian.Uint64(raw))
	}
	return v
}

func elemSize(kind string) int {
	switch kind {
	case "int8", "uint8", "bool":
		return 1
	case "int16", "uint16":
		return 2
	case "int32", "uint32", "float32":
		return 4
	case "int64", "uint64", "float64":
		return 8
	}
	return 0
}

func utf32ToString(raw []byte) string {
	var b strings.Builder
	for i := 0; i+4 <= len(raw); i += 4 {
		b.WriteRune(rune(binary.LittleEndian.Uint32(raw[i : i+4])))
	}
	return b.String()
}

func tlvToJSONValue(kind string, raw []byte, arrLen int) interface{} {
	es := elemSize(kind)
	switch kind {
	case "bool":
		if arrLen > 1 {
			arr := make([]interface{}, arrLen)
			for i := 0; i < arrLen; i++ {
				arr[i] = raw[i] != 0
			}
			return arr
		}
		return raw[0] != 0
	case "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64":
		if arrLen > 1 {
			arr := make([]interface{}, arrLen)
			for i := 0; i < arrLen; i++ {
				arr[i] = readInt(raw[i*es : i*es+es])
			}
			return arr
		}
		return readInt(raw[:es])
	case "float32", "float64":
		if arrLen > 1 {
			arr := make([]interface{}, arrLen)
			for i := 0; i < arrLen; i++ {
				arr[i] = float64From(raw[i*es : i*es+es])
			}
			return arr
		}
		return float64From(raw[:es])
	case "char/utf8", "char/ascii":
		return string(raw)
	case "char/utf32":
		return utf32ToString(raw)
	default:
		return string(raw)
	}
}

func float64From(raw []byte) float64 {
	if len(raw) == 4 {
		return float64(math.Float32frombits(binary.LittleEndian.Uint32(raw)))
	}
	return math.Float64frombits(binary.LittleEndian.Uint64(raw))
}

func jsonValueToTLV(v interface{}) []byte {
	switch t := v.(type) {
	case json.Number:
		if i, err := t.Int64(); err == nil {
			return constructTLV("int64", u64(uint64(i)), 1)
		}
		f, _ := t.Float64()
		return constructTLV("float64", u64bits(f), 1)
	case bool:
		b := byte(0)
		if t {
			b = 1
		}
		return constructTLV("bool", []byte{b}, 1)
	case string:
		return constructTLV("char/utf8", []byte(t), 1)
	default:
		return nil
	}
}

func jsonArrayToTLV(arr []interface{}) []byte {
	if len(arr) == 0 {
		return nil
	}
	switch arr[0].(type) {
	case json.Number:
		allInt := true
		for _, e := range arr {
			if _, err := e.(json.Number).Int64(); err != nil {
				allInt = false
				break
			}
		}
		if allInt {
			raw := make([]byte, 0, len(arr)*8)
			for _, e := range arr {
				i, _ := e.(json.Number).Int64()
				raw = append(raw, u64(uint64(i))...)
			}
			return constructTLV("int64", raw, len(arr))
		}
		raw := make([]byte, 0, len(arr)*8)
		for _, e := range arr {
			f, _ := e.(json.Number).Float64()
			raw = append(raw, u64bits(f)...)
		}
		return constructTLV("float64", raw, len(arr))
	case bool:
		raw := make([]byte, len(arr))
		for i, e := range arr {
			if e.(bool) {
				raw[i] = 1
			}
		}
		return constructTLV("bool", raw, len(arr))
	default:
		return nil
	}
}

func u64(v uint64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, v)
	return b
}

func u64bits(f float64) []byte {
	bits := *(*uint64)(unsafe.Pointer(&f))
	return u64(bits)
}

// ── KV 子树 ↔ map[string]any ───────────────────────────────────────

func splitArrayName(name string) (base string, idx int, ok bool) {
	lt := strings.LastIndex(name, "[")
	if lt <= 0 || !strings.HasSuffix(name, "]") {
		return "", 0, false
	}
	i, err := strconv.Atoi(name[lt+1 : len(name)-1])
	if err != nil {
		return "", 0, false
	}
	return name[:lt], i, true
}

func buildMap(c *C.rwext_conn, root string) map[string]any {
	m := map[string]any{}
	scat := map[string][]int{}
	for _, child := range list(c, root+"/") {
		if child == "" {
			continue
		}
		if base, idx, ok := splitArrayName(child); ok {
			scat[base] = append(scat[base], idx)
			continue
		}
		// 目录：list(child+"/") 非空 → 递归
		if len(list(c, root+"/"+child+"/")) > 0 {
			m[child] = buildMap(c, root+"/"+child)
			continue
		}
		kind, raw, arrLen := parseTLV(getTLV(c, root+"/"+child))
		m[child] = tlvToJSONValue(kind, raw, arrLen)
	}
	for base, idxs := range scat {
		sort.Ints(idxs)
		arr := make([]interface{}, len(idxs))
		for i, idx := range idxs {
			kind, raw, arrLen := parseTLV(getTLV(c, root+"/"+base+"["+strconv.Itoa(idx)+"]"))
			arr[i] = tlvToJSONValue(kind, raw, arrLen)
		}
		m[base] = arr
	}
	return m
}

func writeMap(c *C.rwext_conn, root string, m map[string]any) {
	for k, v := range m {
		childPath := root + "/" + k
		switch t := v.(type) {
		case map[string]any:
			mkindex(c, childPath+"/")
			writeMap(c, childPath, t)
		case []interface{}:
			setTLV(c, childPath, jsonArrayToTLV(t))
		default:
			setTLV(c, childPath, jsonValueToTLV(v))
		}
	}
}

func fromJSON(data []byte) map[string]any {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	var m map[string]any
	if err := dec.Decode(&m); err != nil {
		return nil
	}
	return m
}

// ── rwir handoff ───────────────────────────────────────────────────

type op struct {
	name string
	nr   int
	nw   int
}

var ops = []op{
	{"json.to", 1, 1},
	{"json.from", 1, 1},
}

func register(c *C.rwext_conn) {
	for _, o := range ops {
		sig := "rwir " + o.name + "(a:any) -> (b:any)"
		co := cstr(o.name)
		cs := cstr(sig)
		C.rwext_register(c, co, C.int32_t(o.nr), C.int32_t(o.nw), cs)
		C.free(unsafe.Pointer(co))
		C.free(unsafe.Pointer(cs))
	}
}

func doTo(c *C.rwext_conn, pc string, readNames, writeNames []string) {
	root := readNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveRead(c, pc, 0)
	}
	data, _ := json.Marshal(buildMap(c, root))
	dest := resolveWrite(c, pc, 0)
	setChar(c, dest, string(data))
}

func doFrom(c *C.rwext_conn, pc string, readNames, writeNames []string) {
	src := resolveRead(c, pc, 0)
	root := writeNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveWrite(c, pc, 0)
	}
	writeMap(c, root, fromJSON([]byte(src)))
}

func serveOp(c *C.rwext_conn, o op) {
	base := "/lib/" + o.name
	for _, child := range list(c, base+"/") {
		if !strings.HasPrefix(child, ".todo<") || !strings.HasSuffix(child, ">") {
			continue
		}
		vid := child[6 : len(child)-1]
		todo := base + "/" + child
		pcid := get(c, todo)
		pc, id := pcid, ""
		if i := strings.LastIndex(pcid, "|"); i >= 0 {
			pc, id = pcid[:i], pcid[i+1:]
		}

		ps := params(c, pc)
		opcode := ps[0]
		readNames := ps[1 : 1+o.nr]
		writeNames := ps[1+o.nr : 1+o.nr+o.nw]
		if opcode == "json.to" {
			doTo(c, pc, readNames, writeNames)
		} else {
			doFrom(c, pc, readNames, writeNames)
		}

		nxt := nextPC(pc)
		setChar(c, "/vthread/"+vid+"/‥pc", nxt)
		setChar(c, base+"/.done<"+vid+">", id)
		ck := cstr(todo)
		C.rwext_del(c, ck)
		C.free(unsafe.Pointer(ck))
	}
}

// Serve 常驻循环：注册 + 监控 .todo + 批量执行 + 交还 PC。
func Serve(dsn string) {
	cd := cstr(dsn)
	defer C.free(unsafe.Pointer(cd))
	c := C.rwext_connect(cd)
	if c == nil {
		return
	}
	register(c)
	for {
		for _, o := range ops {
			serveOp(c, o)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

// Package json 是 rwirext 扩展运行时（json.to/json.from）。经 C runtime 的
// rwext_* ABI 与 kvspace 交互（不依赖 kvspace-go）。独立进程常驻 serve。
package json

/*
// 后端（kvspace-c=shm / kvspace_durable=redis|fs）由 Makefile 经 CGO_LDFLAGS 注入，
// 对齐 term 的 KVLANG_KVSPACE_LIB；扩展宿主自连 kvspace，不经 runtime。
#cgo CFLAGS: -I${SRCDIR}/../../../runtime/include
#cgo LDFLAGS: -L${SRCDIR}/../../../bin -lkvlang_runtime -Wl,-rpath,${SRCDIR}/../../../bin
#include "kvlang_rwext.h"
#include <stdint.h>
#include <stdlib.h>

// kvspace ABI（扩展宿主自连，不经 runtime）——runtime .so 传递解析其后端实现。
extern void *kvspaceConnect(const char *dsn);
extern void  kvspaceFree(void *h);
extern void  kvspaceBytesFree(uint8_t *p, uint32_t len);
extern int   kvspaceGet(void *h, const char *key, uint8_t **out, uint32_t *out_len);
extern int   kvspaceSet(void *h, const char *const *keys, const uint8_t *vals,
                        const uint32_t *lens, uint32_t n, char *err, uint32_t err_cap);
extern int   kvspaceList(void *h, const char *prefix, int expand_ext, int resolve,
                         uint8_t **out, uint32_t *out_len);
extern int   kvspaceDel(void *h, const char *const *keys, uint32_t nkeys, char *err, uint32_t err_cap);
extern int   kvspaceMkindex(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspaceNewChar(const char *kind, const char *s, uint8_t **out, uint32_t *out_len);

// XValue 头（repr(C)，对齐 kvspace ABI）：kind+ndim+dims 即 kindexp，body 段靠 offset/len 定位。
typedef struct {
    uint8_t kind[32];
    uint8_t is_ptr;
    int32_t array_len;
    int32_t body_len;
    int32_t body_offset;
    int32_t ndim;
    int32_t dims[8];
} kvspaceHead_t;
extern int   kvspaceDecodeHead(const uint8_t *data, uint32_t data_len, kvspaceHead_t *out);
extern int   kvspaceTlvEncode(const char *kind, const uint8_t *raw, uint32_t raw_len,
                              const int32_t *dims, int32_t ndim, uint8_t **out, uint32_t *out_len);
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

func list(c unsafe.Pointer, prefix string) []string {
	cp := cstr(prefix)
	defer C.free(unsafe.Pointer(cp))
	var out *C.uint8_t
	var outLen C.uint32_t
	if C.kvspaceList(c, cp, 0, 0, &out, &outLen) != 0 || out == nil || outLen == 0 {
		if out != nil {
			C.kvspaceBytesFree(out, outLen)
		}
		return nil
	}
	defer C.kvspaceBytesFree(out, outLen)
	str := C.GoStringN((*C.char)(unsafe.Pointer(out)), C.int(outLen))
	if str == "" {
		return nil
	}
	return strings.Split(str, "\n")
}

func get(c unsafe.Pointer, key string) string {
	_, raw, _ := parseTLV(getTLV(c, key))
	return string(raw)
}

func getTLV(c unsafe.Pointer, key string) []byte {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	var out *C.uint8_t
	var outLen C.uint32_t
	if C.kvspaceGet(c, ck, &out, &outLen) != 0 || out == nil || outLen == 0 {
		if out != nil {
			C.kvspaceBytesFree(out, outLen)
		}
		return nil
	}
	defer C.kvspaceBytesFree(out, outLen)
	return C.GoBytes(unsafe.Pointer(out), C.int(outLen))
}

func setTLV(c unsafe.Pointer, key string, tlv []byte) {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	buf := C.CBytes(tlv)
	defer C.free(buf)
	keys := [1]*C.char{ck}
	lens := [1]C.uint32_t{C.uint32_t(len(tlv))}
	var err [256]C.char
	C.kvspaceSet(c, &keys[0], (*C.uint8_t)(buf), &lens[0], 1, &err[0], 256)
}

func setChar(c unsafe.Pointer, key, val string) {
	ck := cstr("char/utf8")
	cv := cstr(val)
	defer C.free(unsafe.Pointer(ck))
	defer C.free(unsafe.Pointer(cv))
	var out *C.uint8_t
	var outLen C.uint32_t
	if C.kvspaceNewChar(ck, cv, &out, &outLen) != 0 || out == nil {
		return
	}
	defer C.kvspaceBytesFree(out, outLen)
	kek := cstr(key)
	defer C.free(unsafe.Pointer(kek))
	keys := [1]*C.char{kek}
	lens := [1]C.uint32_t{outLen}
	var err [256]C.char
	C.kvspaceSet(c, &keys[0], out, &lens[0], 1, &err[0], 256)
}

func del(c unsafe.Pointer, key string) {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	keys := [1]*C.char{ck}
	var err [256]C.char
	C.kvspaceDel(c, &keys[0], 1, &err[0], 256)
}

func mkindex(c unsafe.Pointer, path string) {
	cp := cstr(path)
	defer C.free(unsafe.Pointer(cp))
	var err [256]C.char
	C.kvspaceMkindex(c, cp, &err[0], 256)
}

func resolveRead(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlang_rwextResolveRead(c, cp, C.int(idx)))
}

func resolveWrite(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlang_rwextResolveWrite(c, cp, C.int(idx)))
}

func nextPC(pc string) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlang_rwextNextPc(cp))
}

func params(c unsafe.Pointer, pc string) []string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return strings.Split(gostr(C.kvlang_rwextParams(c, cp)), "\n")
}

// ── XValue 编解码（走权威 kvspace ABI：DecodeHead 读头 + TlvEncode/NewChar 编码）──

// parseTLV：kvspaceDecodeHead 解出 kind/ndim/dims，body 段即元素平铺；arrLen = ∏dims（标量为 1）。
func parseTLV(data []byte) (kind string, raw []byte, arrLen int) {
	if len(data) == 0 {
		return "", nil, 0
	}
	var h C.kvspaceHead_t
	if C.kvspaceDecodeHead((*C.uint8_t)(unsafe.Pointer(&data[0])), C.uint32_t(len(data)), &h) != 0 {
		return "", nil, 0
	}
	kb := C.GoBytes(unsafe.Pointer(&h.kind[0]), 32)
	if i := bytes.IndexByte(kb, 0); i >= 0 {
		kb = kb[:i]
	}
	kind = string(kb)
	bo, bl := int(h.body_offset), int(h.body_len)
	if bo < 0 || bl < 0 || bo+bl > len(data) {
		return kind, nil, 1
	}
	raw = data[bo : bo+bl]
	arrLen = 1
	for i := 0; i < int(h.ndim); i++ {
		arrLen *= int(h.dims[i])
	}
	if arrLen < 1 {
		arrLen = 1
	}
	return kind, raw, arrLen
}

// constructTLV：char/* 走 kvspaceNewChar，数值/布尔走 kvspaceTlvEncode（arrLen>1 → 一维 [arrLen]）。
func constructTLV(kind string, raw []byte, arrLen int) []byte {
	if strings.HasPrefix(kind, "char/") {
		ck := cstr(kind)
		cv := C.CBytes(append(append([]byte{}, raw...), 0)) // NUL 结尾
		defer C.free(unsafe.Pointer(ck))
		defer C.free(cv)
		var out *C.uint8_t
		var ol C.uint32_t
		if C.kvspaceNewChar(ck, (*C.char)(cv), &out, &ol) != 0 || out == nil {
			return nil
		}
		defer C.kvspaceBytesFree(out, ol)
		return C.GoBytes(unsafe.Pointer(out), C.int(ol))
	}
	ck := cstr(kind)
	defer C.free(unsafe.Pointer(ck))
	var buf unsafe.Pointer
	if len(raw) > 0 {
		buf = C.CBytes(raw)
		defer C.free(buf)
	}
	d := [1]C.int32_t{C.int32_t(arrLen)}
	var dims *C.int32_t
	var ndim C.int32_t
	if arrLen > 1 {
		dims = &d[0]
		ndim = 1
	}
	var out *C.uint8_t
	var ol C.uint32_t
	if C.kvspaceTlvEncode(ck, (*C.uint8_t)(buf), C.uint32_t(len(raw)), dims, ndim, &out, &ol) != 0 || out == nil {
		return nil
	}
	defer C.kvspaceBytesFree(out, ol)
	return C.GoBytes(unsafe.Pointer(out), C.int(ol))
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

func buildMap(c unsafe.Pointer, root string) map[string]any {
	m := map[string]any{}
	scat := map[string][]int{}
	for _, child := range list(c, root+"/") {
		if child == "" {
			continue
		}
		if strings.HasSuffix(child, "/") { // index 目录 → 递归
			name := strings.TrimSuffix(child, "/")
			m[name] = buildMap(c, root+"/"+name)
			continue
		}
		if base, idx, ok := splitArrayName(child); ok {
			scat[base] = append(scat[base], idx)
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

func writeMap(c unsafe.Pointer, root string, m map[string]any) {
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

func register(c unsafe.Pointer) {
	for _, o := range ops {
		sig := strings.TrimSuffix(strings.Repeat("any\n", o.nr+o.nw), "\n")
		co := cstr(o.name)
		cs := cstr(sig)
		C.kvlang_rwextRegister(c, co, C.int32_t(o.nr), C.int32_t(o.nw), cs)
		C.free(unsafe.Pointer(co))
		C.free(unsafe.Pointer(cs))
	}
}

func doTo(c unsafe.Pointer, pc string, readNames, writeNames []string) {
	root := readNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveRead(c, pc, 0)
	}
	data, _ := json.Marshal(buildMap(c, root))
	dest := resolveWrite(c, pc, 0)
	setChar(c, dest, string(data))
}

func doFrom(c unsafe.Pointer, pc string, readNames, writeNames []string) {
	src := resolveRead(c, pc, 0)
	root := writeNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveWrite(c, pc, 0)
	}
	writeMap(c, root, fromJSON([]byte(src)))
}

func serveOp(c unsafe.Pointer, o op) {
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
		del(c, todo)
	}
}

// Serve 常驻循环：注册 + 监控 .todo + 批量执行 + 交还 PC。
func Serve(dsn string) {
	cd := cstr(dsn)
	defer C.free(unsafe.Pointer(cd))
	c := C.kvspaceConnect(cd)
	if c == nil {
		return
	}
	defer C.kvspaceFree(c)
	register(c)
	for {
		for _, o := range ops {
			serveOp(c, o)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

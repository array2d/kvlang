// Package json 是 rwirext 扩展运行时（json.to/json.from）。经 C runtime 的
// rwirext_* ABI 与 kvspace 交互（不依赖 kvspace-go）。独立进程常驻 serve。
package json

/*
// 后端（kvspace-c=shm / kvspace_durable=redis|fs）由 Makefile 经 CGO_LDFLAGS 注入，
// 对齐 term 的 KVLANG_KVSPACE_LIB；扩展宿主自连 kvspace，不经 runtime。
#cgo CFLAGS: -I${SRCDIR}/../../../runtime/include
#cgo LDFLAGS: -L${SRCDIR}/../../../bin -lkvlang_runtime -L${SRCDIR}/../../../layout/target/release -lkvlang_layout -Wl,-rpath,${SRCDIR}/../../../bin -Wl,-rpath,${SRCDIR}/../../../layout/target/release
#include "kvlang_rwirext.h"
#include <stdint.h>
#include <stdlib.h>

// kvspace ABI（扩展宿主自连，不经 runtime）——runtime .so 传递解析其后端实现。
extern void *kvspaceConnect(const char *dsn);
extern void  kvspaceClose(void *h);
extern void  kvspaceBytesFree(uint8_t *p, uint32_t len);
extern int   kvspaceGet(void *h, const char *key, uint8_t **out, uint32_t *out_len);
extern int   kvspaceSet(void *h, const char *const *keys, const uint8_t *vals,
                        const uint32_t *lens, uint32_t n, char *err, uint32_t err_cap);
extern int   kvspaceList(void *h, const char *prefix, int expand_ext, int resolve,
                         uint8_t **out, uint32_t *out_len);
extern int   kvspaceDel(void *h, const char *const *keys, uint32_t nkeys, char *err, uint32_t err_cap);
extern int   kvspaceDelTree(void *h, const char *prefix, char *err, uint32_t err_cap);
extern int   kvspaceMkindex(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspaceNewChar(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len);
extern const char *kvspaceConst(const char *name);

// XValue 头（repr(C)，对齐 kvspace ABI）：kindexpr 为唯一类型真相，body 段靠 offset/len 定位。
typedef struct {
    uint8_t  kindexpr[256];
    uint8_t  ro;
    uint32_t vid;
    int32_t  body_len;
    int32_t  body_offset;
} kvspaceHead_t;
extern int   kvspaceDecodeHead(const uint8_t *data, uint32_t data_len, kvspaceHead_t *out);
extern int   kvspaceTlvEncode(const char *kind, const uint8_t *raw, uint32_t raw_len,
                              const int32_t *dims, int32_t ndim, uint8_t **out, uint32_t *out_len);

// kindexpr 解析（kvlang/layout 提供 ABI，唯一事实源）：ref/ndim/dims/kind。
typedef struct {
    int32_t ref;
    int32_t ndim;
    int32_t dims[8];
    int32_t array_len;
    uint8_t kind[64];
} kvlangKindexpr;
extern int   kvlangKindexprParse(const char *kindexpr, kvlangKindexpr *out);
*/
import "C"

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
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
	cv := C.CBytes([]byte(val))
	defer C.free(cv)
	var out *C.uint8_t
	var outLen C.uint32_t
	if C.kvspaceNewChar((*C.uint8_t)(cv), C.uint32_t(len(val)), &out, &outLen) != 0 || out == nil {
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

func delTree(c unsafe.Pointer, prefix string) {
	cp := cstr(prefix)
	defer C.free(unsafe.Pointer(cp))
	var err [256]C.char
	C.kvspaceDelTree(c, cp, &err[0], 256)
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
	return gostr(C.kvlang_rwirextResolveRead(c, cp, C.int(idx)))
}

func resolveReadPath(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlang_rwirextResolveReadPath(c, cp, C.int(idx)))
}

func resolveWrite(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlang_rwirextResolveWrite(c, cp, C.int(idx)))
}

func nextPC(pc string) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlang_rwirextNextPc(cp))
}

func params(c unsafe.Pointer, pc string) []string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return strings.Split(gostr(C.kvlang_rwirextParams(c, cp)), "\n")
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
	var kx C.kvlangKindexpr
	if C.kvlangKindexprParse((*C.char)(unsafe.Pointer(&h.kindexpr[0])), &kx) != 0 {
		return "", nil, 0
	}
	kb := C.GoBytes(unsafe.Pointer(&kx.kind[0]), 64)
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
	for i := 0; i < int(kx.ndim); i++ {
		arrLen *= int(kx.dims[i])
	}
	if arrLen < 1 {
		arrLen = 1
	}
	return kind, raw, arrLen
}

// constructTLV：char/utf8 走 kvspaceNewChar（显式长度，NUL 安全），
// 其余（数值/布尔/object 等）走 kvspaceTlvEncode（arrLen>1 → 一维 [arrLen]）。
func constructTLV(kind string, raw []byte, arrLen int) []byte {
	if kind == kindChar8 {
		buf := C.CBytes(raw)
		defer C.free(buf)
		var out *C.uint8_t
		var ol C.uint32_t
		if C.kvspaceNewChar((*C.uint8_t)(buf), C.uint32_t(len(raw)), &out, &ol) != 0 || out == nil {
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

// encodeTLVDims：显式 dims/ndim 编码（供 stringkeymap 容器值落 dims）。
func encodeTLVDims(kind string, raw []byte, dims []int32) []byte {
	ck := cstr(kind)
	defer C.free(unsafe.Pointer(ck))
	var buf unsafe.Pointer
	if len(raw) > 0 {
		buf = C.CBytes(raw)
		defer C.free(buf)
	}
	carr := make([]C.int32_t, len(dims))
	for i, d := range dims {
		carr[i] = C.int32_t(d)
	}
	var out *C.uint8_t
	var ol C.uint32_t
	if C.kvspaceTlvEncode(ck, (*C.uint8_t)(buf), C.uint32_t(len(raw)), &carr[0], C.int32_t(len(carr)), &out, &ol) != 0 || out == nil {
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
				arr[i] = floatJSON(float64From(raw[i*es : i*es+es]))
			}
			return arr
		}
		return floatJSON(float64From(raw[:es]))
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

// floatJSON：float 导出保形——数学上为整数的值带小数点（1.0 而非 1），-0.0 保符号，
// 使二次往返的 kind 不漂移（float64 整数不再被误读成 int64）。
func floatJSON(f float64) json.Number {
	if math.IsInf(f, 0) || math.IsNaN(f) {
		return json.Number(strconv.FormatFloat(f, 'g', -1, 64))
	}
	if f == 0 && math.Signbit(f) {
		return json.Number("-0.0")
	}
	s := strconv.FormatFloat(f, 'g', -1, 64)
	if !strings.ContainsAny(s, ".eE") {
		return json.Number(s + ".0")
	}
	return json.Number(s)
}

func jsonValueToTLV(v interface{}) ([]byte, error) {
	switch t := v.(type) {
	case json.Number:
		s := t.String()
		if i, err := t.Int64(); err == nil {
			return constructTLV(kindInt64, u64(uint64(i)), 1), nil
		}
		// 纯整数文本（无 . e E）但 Int64 失败 → 超 int64，显式拒绝，不静默降 float64。
		if !strings.ContainsAny(s, ".eE") {
			return nil, fmt.Errorf("json: integer %s overflows int64", s)
		}
		f, _ := t.Float64()
		return constructTLV(kindFloat64, u64bits(f), 1), nil
	case bool:
		b := byte(0)
		if t {
			b = 1
		}
		return constructTLV(kindBool, []byte{b}, 1), nil
	case string:
		return constructTLV(kindChar8, []byte(t), 1), nil
	default:
		return nil, fmt.Errorf("json: unsupported value type %T", v)
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
// 值/索引分离：容器值（object/stringkeymap）存 p（无后缀，body 空，stringkeymap 的 dims 在 head）；
// memindex 存 p·（kind=index，body=[4B count][names]，成员列表唯一权威）；成员在 p·<key>
// （key 恒字符串，数组下标为坐标段 [i]）。后端按 · 成员自动维护 index 与 stringkeymap 兜底。

// 从 kvspace ABI（kvspaceConst）取的常量，扩展不硬编码分隔符/kind 字面量（#111）。
var (
	sep         string
	runtimeSep  string
	dirSuf      string
	indexSep    string
	kindObj     string
	kindMap     string
	kindIndex   string
	kindChar8   string
	kindInt64   string
	kindFloat64 string
	kindBool    string
)

func init() {
	sep = cconst("KVSPACE_MEMBER_SEP")
	runtimeSep = cconst("KVSPACE_RUNTIME_MEMBER_SEP")
	dirSuf = cconst("KVSPACE_DIR_INDEX_SUF")
	indexSep = cconst("KVSPACE_INDEX_VALUE_SEP")
	kindObj = cconst("KVSPACE_KIND_OBJ")
	kindMap = cconst("KVSPACE_KIND_MAP")
	kindIndex = cconst("KVSPACE_KIND_INDEX")
	kindChar8 = cconst("KVSPACE_KIND_CHAR_UTF8")
	kindInt64 = cconst("KVSPACE_KIND_INT64")
	kindFloat64 = cconst("KVSPACE_KIND_FLOAT64")
	kindBool = cconst("KVSPACE_KIND_BOOL")

	myrwircaps = []op{
		{"json" + sep + "to", 1, 1},
		{"json" + sep + "from", 1, 1},
	}
}

func cconst(name string) string {
	cn := cstr(name)
	defer C.free(unsafe.Pointer(cn))
	s := C.kvspaceConst(cn)
	if s == nil {
		return ""
	}
	// kvspaceConst 返回静态字符串，不得 free。
	return C.GoString(s)
}

// memindex p·：kind=index，body=[4B count LE][names]，成员列表唯一权威。
func mkMemIndex(names []string) []byte {
	body := make([]byte, 4)
	binary.LittleEndian.PutUint32(body, uint32(len(names)))
	body = append(body, []byte(strings.Join(names, "\n"))...)
	return constructTLV(kindIndex, body, 1)
}

// 容器值（无后缀 p）：object body 空。
func mkObjValue() []byte { return constructTLV(kindObj, nil, 1) }

// 容器值（无后缀 p）：stringkeymap body 空，dims=[n]（恒一维坐标段 [i]）。
func mkMapValue(n int) []byte { return encodeTLVDims(kindMap, nil, []int32{int32(n)}) }

// validateKey：JSON 对象 key 不能含影响 kvspace 存储分隔的字符（§5.4）。
// 空串、/ · [ ] \n \r \0 U+2025 及 ASCII 控制字符一律拒绝（不静默丢键、不转义）。
// '.' 已释放给小数 key，可作成员名。
func validateKey(k string) error {
	if k == "" {
		return fmt.Errorf("json: empty key rejected")
	}
	for _, r := range k {
		if r == '[' || r == ']' || r == '\r' || r == 0 || r < 0x20 ||
			strings.ContainsRune(sep+dirSuf+indexSep+runtimeSep, r) {
			return fmt.Errorf("json: forbidden char %q in key %q", r, k)
		}
	}
	return nil
}

func writeValue(c unsafe.Pointer, path string, v interface{}) error {
	switch t := v.(type) {
	case nil:
		setTLV(c, path, nil) // None：key 存在、值为空字节（JSON null）
	case map[string]any:
		return writeObj(c, path, t)
	case []interface{}:
		return writeArr(c, path, t)
	default:
		tlv, err := jsonValueToTLV(v)
		if err != nil {
			return err
		}
		setTLV(c, path, tlv)
	}
	return nil
}

func writeObj(c unsafe.Pointer, path string, m map[string]any) error {
	keys := make([]string, 0, len(m))
	for k := range m {
		if err := validateKey(k); err != nil {
			return err
		}
		keys = append(keys, k)
	}
	sort.Strings(keys)
	setTLV(c, path, mkObjValue())
	setTLV(c, path+sep, mkMemIndex(keys))
	for _, k := range keys {
		if err := writeValue(c, path+sep+k, m[k]); err != nil {
			return err
		}
	}
	return nil
}

func writeArr(c unsafe.Pointer, path string, arr []interface{}) error {
	keys := make([]string, len(arr))
	for i := range arr {
		keys[i] = fmt.Sprintf("[%d]", i)
	}
	setTLV(c, path, mkMapValue(len(arr)))
	setTLV(c, path+sep, mkMemIndex(keys))
	for i, v := range arr {
		if err := writeValue(c, path+sep+fmt.Sprintf("[%d]", i), v); err != nil {
			return err
		}
	}
	return nil
}

func readValue(c unsafe.Pointer, path string) interface{} {
	// 容器值在 p（无后缀）：object/stringkeymap（body 空，成员在 memindex p·）
	kind, _, _ := parseTLV(getTLV(c, path))
	if kind == kindObj {
		return readObj(c, path)
	}
	if kind == kindMap {
		return readArr(c, path)
	}
	// / 目录树（kind=index）
	if dkind, _, _ := parseTLV(getTLV(c, path+dirSuf)); dkind == kindIndex {
		return readDir(c, path)
	}
	// 单值（标量 / compact ndarray / 字符串）
	kind, raw, arrLen := parseTLV(getTLV(c, path))
	if kind == "" {
		return nil // None → JSON null
	}
	return tlvToJSONValue(kind, raw, arrLen)
}

// readDir：/ 目录树（kind=index）→ JSON object，子节点递归；子名带尾 /（子目录）或 ·（成员目录）先 strip。
func readDir(c unsafe.Pointer, path string) map[string]any {
	m := map[string]any{}
	for _, name := range list(c, path+dirSuf) {
		key := strings.TrimSuffix(strings.TrimSuffix(name, dirSuf), sep)
		m[key] = readValue(c, path+dirSuf+key)
	}
	return m
}

func readObj(c unsafe.Pointer, path string) map[string]any {
	m := map[string]any{}
	for _, name := range list(c, path+sep) {
		m[name] = readValue(c, path+sep+name)
	}
	return m
}

func readArr(c unsafe.Pointer, path string) []interface{} {
	idxs := make([]int, 0, 8)
	for _, n := range list(c, path+sep) {
		s := strings.TrimPrefix(n, "[")
		s = strings.TrimSuffix(s, "]")
		if i, err := strconv.Atoi(s); err == nil {
			idxs = append(idxs, i)
		}
	}
	sort.Ints(idxs)
	arr := make([]interface{}, len(idxs))
	for i, idx := range idxs {
		arr[i] = readValue(c, path+sep+fmt.Sprintf("[%d]", idx))
	}
	return arr
}

// write：顶层写入（覆盖语义，root 子树等于 src）。v 可为 map/slice/标量/nil。
func write(c unsafe.Pointer, root string, v interface{}) error {
	if root != "" && root != "/" {
		delTree(c, root)
	}
	return writeValue(c, root, v)
}

func build(c unsafe.Pointer, root string) interface{} {
	return readValue(c, root)
}

func fromJSON(data []byte) (interface{}, error) {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	var v interface{}
	if err := dec.Decode(&v); err != nil {
		return nil, err
	}
	return v, nil
}

// ── rwir handoff ───────────────────────────────────────────────────

type op struct {
	name string
	nr   int
	nw   int
}

var myrwircaps []op

func register(c unsafe.Pointer) {
	for _, o := range myrwircaps {
		sig := strings.TrimSuffix(strings.Repeat("any\n", o.nr+o.nw), "\n")
		co := cstr(o.name)
		cs := cstr(sig)
		C.kvlang_rwirextRegister(c, co, C.int32_t(o.nr), C.int32_t(o.nw), cs)
		C.free(unsafe.Pointer(co))
		C.free(unsafe.Pointer(cs))
	}
}

func doTo(c unsafe.Pointer, pc string, readNames, writeNames []string) {
	root := readNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveReadPath(c, pc, 0)
	}
	data, _ := json.Marshal(build(c, root))
	dest := resolveWrite(c, pc, 0)
	setChar(c, dest, string(data))
}

func doFrom(c unsafe.Pointer, pc string, readNames, writeNames []string, vid string) {
	src := resolveRead(c, pc, 0)
	root := writeNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveWrite(c, pc, 0)
	}
	v, err := fromJSON([]byte(src))
	if err != nil {
		setChar(c, "/vthread/"+vid+"/"+runtimeSep+"status", "error")
		setChar(c, "/vthread/"+vid+"/"+runtimeSep+"error/msg", err.Error())
		return
	}
	if err := write(c, root, v); err != nil {
		setChar(c, "/vthread/"+vid+"/"+runtimeSep+"status", "error")
		setChar(c, "/vthread/"+vid+"/"+runtimeSep+"error/msg", err.Error())
		return
	}
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
		if opcode == "json"+sep+"to" {
			doTo(c, pc, readNames, writeNames)
		} else {
			doFrom(c, pc, readNames, writeNames, vid)
		}

		nxt := nextPC(pc)
		setChar(c, "/vthread/"+vid+"/"+runtimeSep+"pc", nxt)
		setChar(c, base+"/.done<"+vid+">", id)
		del(c, todo)
	}
}

func connect(dsn string) unsafe.Pointer {
	cd := cstr(dsn)
	defer C.free(unsafe.Pointer(cd))
	return C.kvspaceConnect(cd)
}

func disconnect(c unsafe.Pointer) {
	C.kvspaceClose(c)
}

// Serve 常驻循环：注册 + 监控 .todo + 批量执行 + 交还 PC。
func Serve(dsn string) {
	c := connect(dsn)
	if c == nil {
		return
	}
	defer C.kvspaceClose(c)
	register(c)
	for {
		for _, o := range myrwircaps {
			serveOp(c, o)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

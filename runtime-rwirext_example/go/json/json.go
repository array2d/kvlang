// Package json 是 rwirext 扩展运行时（json.to/json.from）。经 C runtime 的
// rwirext_* ABI 取指令语义，经 kvspace 正典 ABI 读写 KV（扩展宿主自连，不经 runtime）。
package json

/*
// 后端（shm→kvspace-c / redis|fs→kvspace_durable）由 libkvspace dispatch 前端按 DSN 选，
// 经 CGO_LDFLAGS 注入。
#cgo CFLAGS: -I${SRCDIR}/../../../runtime/include
#cgo LDFLAGS: -L${SRCDIR}/../../../bin -lkvlang_runtime -Wl,-rpath,${SRCDIR}/../../../bin
#include "kvlang_runtime.h"
#include <stdint.h>
#include <stdlib.h>

// kvspace 正典 ABI（30 符号）。读恒借用（不得 free）；codec 产出为前端 malloc（libc free）。
extern void *kvspaceConnect(const char *dsn);
extern void  kvspaceClose(void *h);
extern int   kvspaceGet(void *h, const char *key, int resolve, uint8_t **out, uint32_t *out_len);
extern int   kvspaceWriteInPlace(void *h, const char *key, int resolve, uint32_t body_len,
                                 uint8_t **body, char *err, uint32_t err_cap);
extern int   kvspaceWriteNewPlace(void *h, const char *key, uint8_t ref, uint8_t storetype,
                                  uint8_t ro, uint32_t vid, const char *langtype, uint32_t body_len,
                                  uint8_t **body, char *err, uint32_t err_cap);
extern int   kvspaceListLen(void *h, const char *prefix, int expand_ext, int resolve, int32_t *out_count);
extern int   kvspaceListAt(void *h, const char *prefix, int expand_ext, int resolve, int32_t idx,
                           uint8_t *buf, uint32_t buf_cap, uint32_t *out_len);
extern int   kvspaceDel(void *h, const char *const *keys, uint32_t nkeys, char *err, uint32_t err_cap);
extern int   kvspaceDelTree(void *h, const char *prefix, char *err, uint32_t err_cap);
extern int   kvspaceMkindex(void *h, const char *path, uint32_t capacity, char *err, uint32_t err_cap);
extern int   kvspaceTlvEncode(const char *kind, const uint8_t *raw, uint32_t raw_len,
                              const int32_t *dims, int32_t ndim, uint8_t **out, uint32_t *out_len);
extern int   kvspaceNewChar(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len);
extern const char *kvspaceConst(const char *name);

// XValue 头（逐字段对齐 kvspace/include/kvspace/kvspace.h）：三正交轴 ref×storetype×langtype。
typedef struct {
    uint16_t headlen;
    uint8_t  ref;
    uint8_t  storetype;
    uint8_t  ro;
    uint32_t vid;
    int32_t  body_len;
    int32_t  ndim;
    int32_t  dims[8];
    char     langtype[256];
    int32_t  langtype_len;
    int32_t  body_offset;
} kvspaceHead_t;
extern int kvspaceDecodeHead(const uint8_t *data, uint32_t data_len, kvspaceHead_t *out);
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

// storetype：容器值恒 index（spec map容器）；extindex 同 index 系，读判共用。
const (
	storetypeIndex    = 3
	storetypeExtIndex = 4
	refPtr            = 1
)

// ── cgo 封装 ────────────────────────────────────────────────────────

func cstr(s string) *C.char { return C.CString(s) }

func gostr(s *C.char) string {
	if s == nil {
		return ""
	}
	defer C.free(unsafe.Pointer(s)) // rwirext ABI 产出为 malloc，须 free
	return C.GoString(s)
}

func headOf(data []byte) (C.kvspaceHead_t, bool) {
	var h C.kvspaceHead_t
	if len(data) == 0 {
		return h, false
	}
	if C.kvspaceDecodeHead((*C.uint8_t)(unsafe.Pointer(&data[0])), C.uint32_t(len(data)), &h) != 0 {
		return h, false
	}
	return h, true
}

// langtypeOf：head 的完整 kindexpr 串（含 [dims] 前缀）。
func langtypeOf(h *C.kvspaceHead_t) string { return C.GoString(&h.langtype[0]) }

// baseKind：剥去 [dims] 形状前缀后的种类名。`·` 分隔的 map langtype 原样返回（键侧方括号是
// 键类型，不是形状，见 spec map容器）。
func baseKind(lt string) string {
	if strings.Contains(lt, sep) {
		return lt
	}
	if strings.HasPrefix(lt, "[") {
		if i := strings.IndexByte(lt, ']'); i >= 0 {
			return lt[i+1:]
		}
	}
	return lt
}

// isMapLangtype：值容器判定 —— 裸种类名 stringkeymap，或完整 map langtype（含 ·）。
func isMapLangtype(lt string) bool {
	k := baseKind(lt)
	return k == kindMap || strings.Contains(k, sep)
}

// getTLV：借用读。resolve=1 穿透 link（handoff 队列经 Ptr 统一到首个 op，须穿透）。
// 返回拷贝，调用方持有；借用指针不得 free。
func getTLV(c unsafe.Pointer, key string, resolve int) []byte {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	var out *C.uint8_t
	var outLen C.uint32_t
	if C.kvspaceGet(c, ck, C.int(resolve), &out, &outLen) != 0 || out == nil || outLen == 0 {
		return nil
	}
	return C.GoBytes(unsafe.Pointer(out), C.int(outLen))
}

func headAt(c unsafe.Pointer, key string) (C.kvspaceHead_t, bool) {
	return headOf(getTLV(c, key, 0))
}

// get：key 的 body 字节（借用读 → 拷贝）。
func get(c unsafe.Pointer, key string, resolve int) string {
	data := getTLV(c, key, resolve)
	h, ok := headOf(data)
	if !ok {
		return ""
	}
	bo, bl := int(h.body_offset), int(h.body_len)
	if bo < 0 || bl < 0 || bo+bl > len(data) {
		return ""
	}
	return string(data[bo : bo+bl])
}

// writeBody：写即构造——按三正交轴 (ref, storetype, ro, vid, langtype) + body 向后端要
// body 偏移指针后直接写字节。key 已存在且三轴与 body_len 全同 → WriteInPlace（原 box 就地）；
// 否则 WriteNewPlace（新 box）。
func writeBody(c unsafe.Pointer, key string, ref, storetype, ro C.uint8_t, vid C.uint32_t,
	langtype string, body []byte) {
	ck := cstr(key)
	defer C.free(unsafe.Pointer(ck))
	cl := cstr(langtype)
	defer C.free(unsafe.Pointer(cl))
	n := C.uint32_t(len(body))
	var dst *C.uint8_t
	var err [256]C.char
	place := C.int(1)
	if cur, ok := headAt(c, key); ok &&
		cur.ref == ref && cur.storetype == storetype &&
		int(cur.body_len) == len(body) && langtypeOf(&cur) == langtype {
		place = C.kvspaceWriteInPlace(c, ck, 0, n, &dst, &err[0], 256)
	}
	if place != 0 {
		if C.kvspaceWriteNewPlace(c, ck, ref, storetype, ro, vid, cl, n, &dst, &err[0], 256) != 0 {
			return
		}
	}
	if len(body) > 0 && dst != nil {
		copy(unsafe.Slice((*byte)(unsafe.Pointer(dst)), len(body)), body)
	}
}

// setTLV：解 head 取三轴 + body，走写即构造。
func setTLV(c unsafe.Pointer, key string, tlv []byte) {
	h, ok := headOf(tlv)
	if !ok {
		return
	}
	bo, bl := int(h.body_offset), int(h.body_len)
	var body []byte
	if bo >= 0 && bl >= 0 && bo+bl <= len(tlv) {
		body = tlv[bo : bo+bl]
	}
	writeBody(c, key, h.ref, h.storetype, h.ro, h.vid, langtypeOf(&h), body)
}

// setNone：None（JSON null）落 storetype=NONE、langtype=""、body 空。
func setNone(c unsafe.Pointer, key string) {
	writeBody(c, key, 0, 0, 0, 0, "", nil)
}

// list：前缀直接子项名（ListLen 定计数 + 逐 idx ListAt 取名，借用缓冲不 free）。
func list(c unsafe.Pointer, prefix string, resolve int) []string {
	cp := cstr(prefix)
	defer C.free(unsafe.Pointer(cp))
	var count C.int32_t
	if C.kvspaceListLen(c, cp, 0, C.int(resolve), &count) != 0 || count <= 0 {
		return nil
	}
	names := make([]string, 0, int(count))
	buf := make([]byte, 1024)
	for i := C.int32_t(0); i < count; i++ {
		var n C.uint32_t
		if C.kvspaceListAt(c, cp, 0, C.int(resolve), i, (*C.uint8_t)(unsafe.Pointer(&buf[0])), C.uint32_t(len(buf)), &n) != 0 {
			if n <= C.uint32_t(len(buf)) {
				continue
			}
			buf = make([]byte, int(n))
			if C.kvspaceListAt(c, cp, 0, C.int(resolve), i, (*C.uint8_t)(unsafe.Pointer(&buf[0])), C.uint32_t(len(buf)), &n) != 0 {
				continue
			}
		}
		if n > 0 {
			names = append(names, string(buf[:n]))
		}
	}
	return names
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
	C.kvspaceMkindex(c, cp, 0, &err[0], 256)
}

func resolveRead(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlangRwirextResolveRead(c, cp, C.int(idx)))
}

func resolveReadPath(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlangRwirextResolveReadPath(c, cp, C.int(idx)))
}

func resolveWrite(c unsafe.Pointer, pc string, idx int) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlangRwirextResolveWrite(c, cp, C.int(idx)))
}

func nextPC(pc string) string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return gostr(C.kvlangRwirextNextPc(cp))
}

func params(c unsafe.Pointer, pc string) []string {
	cp := cstr(pc)
	defer C.free(unsafe.Pointer(cp))
	return strings.Split(gostr(C.kvlangRwirextParams(c, cp)), "\n")
}

// ── XValue 编解码（走权威 codec：DecodeHead 读头 + TlvEncode/NewChar 编码）──

// parseTLV：解 head 得 kind/body/arrLen。arrLen = ∏物理 dims（标量为 1；容器值 body 空）。
func parseTLV(data []byte) (kind string, raw []byte, arrLen int) {
	h, ok := headOf(data)
	if !ok {
		return "", nil, 0
	}
	kind = baseKind(langtypeOf(&h))
	bo, bl := int(h.body_offset), int(h.body_len)
	if bo < 0 || bl < 0 || bo+bl > len(data) {
		return kind, nil, 1
	}
	raw = data[bo : bo+bl]
	arrLen = 1
	for i := 0; i < int(h.ndim) && i < 8; i++ {
		arrLen *= int(h.dims[i])
	}
	if arrLen < 1 {
		arrLen = 1
	}
	return kind, raw, arrLen
}

// encodeTLV：类型化编码（唯一编码入口，委托 kvspace 正典 codec，不手拼 head）。
func encodeTLV(kind string, raw []byte, dims []int32) []byte {
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
	var dp *C.int32_t
	if len(dims) > 0 {
		dp = &carr[0]
	}
	var out *C.uint8_t
	var ol C.uint32_t
	if C.kvspaceTlvEncode(ck, (*C.uint8_t)(buf), C.uint32_t(len(raw)), dp, C.int32_t(len(dims)), &out, &ol) != 0 || out == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(out)) // codec 产出为前端 malloc
	return C.GoBytes(unsafe.Pointer(out), C.int(ol))
}

// constructTLV：标量（arrLen==1）或一维数组（arrLen>1 → dims=[arrLen]）。
func constructTLV(kind string, raw []byte, arrLen int) []byte {
	if arrLen > 1 {
		return encodeTLV(kind, raw, []int32{int32(arrLen)})
	}
	return encodeTLV(kind, raw, nil)
}

// constructChar：字符串走 codec 的 NewChar（显式长度，NUL 安全）。空串亦须给非空指针。
func constructChar(raw []byte) []byte {
	n := len(raw)
	if n == 0 {
		n = 1
	}
	buf := C.malloc(C.size_t(n))
	defer C.free(buf)
	if len(raw) > 0 {
		copy(unsafe.Slice((*byte)(buf), len(raw)), raw)
	}
	var out *C.uint8_t
	var ol C.uint32_t
	if C.kvspaceNewChar((*C.uint8_t)(buf), C.uint32_t(len(raw)), &out, &ol) != 0 || out == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(out))
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
		if len(raw) == 0 {
			return false
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
		if len(raw) < es {
			return int64(0)
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
		if len(raw) < es {
			return floatJSON(0)
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
	if len(raw) < 8 {
		return 0
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
		return constructChar([]byte(t)), nil
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

// ── KV 子树 ↔ JSON ─────────────────────────────────────────────────
// 容器值落裸 base（p，body 空）；成员落 p·<key>（· 成员目录）；memindex p· 由后端按成员自动维护
// （nv= 值容器写入时建，成员写入时增删），扩展不手写索引。
// 容器判定与对象/数组分派只看容器值 head：map langtype（或裸种类名 stringkeymap）即容器；
// 成员名全为坐标段 `[i]` → JSON 数组，否则命名字典 → JSON 对象；空容器按 dims（数组 dims=[n]、
// 对象无 dims）分派，使空对象 {} 与空数组 [] 往返不混。

var (
	sep         string
	runtimeSep  string
	dirSuf      string
	kindMap     string
	kindInt64   string
	kindFloat64 string
	kindBool    string
)

func init() {
	sep = cconst("KVSPACE_MEMBER_SEP")
	runtimeSep = cconst("KVSPACE_RUNTIME_MEMBER_SEP")
	dirSuf = cconst("KVSPACE_DIR_INDEX_SUF")
	kindMap = cconst("KVSPACE_KIND_MAP")
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

// validateKey：JSON 对象 key 不能含影响 kvspace 存储分隔的字符（spec 成员名字符约束）。
// 空串、/ · [ ] \n \r \0 U+2025 U+2026 及 ASCII 控制字符一律拒绝（不静默丢键、不转义）。
// '.' 已释放给小数 key，可作成员名。
func validateKey(k string) error {
	if k == "" {
		return fmt.Errorf("json: empty key rejected")
	}
	for _, r := range k {
		if r == '[' || r == ']' || r == '\r' || r == 0 || r < 0x20 || r == 0x2025 || r == 0x2026 ||
			strings.ContainsRune(sep+dirSuf, r) {
			return fmt.Errorf("json: forbidden char %q in key %q", r, k)
		}
	}
	return nil
}

// coordIndex：坐标段 `[i]` → i；非单一整数坐标返回 false。
func coordIndex(name string) (int, bool) {
	if len(name) < 3 || name[0] != '[' || name[len(name)-1] != ']' {
		return 0, false
	}
	i, err := strconv.Atoi(name[1 : len(name)-1])
	if err != nil || i < 0 {
		return 0, false
	}
	return i, true
}

func writeValue(c unsafe.Pointer, path string, v interface{}) error {
	switch t := v.(type) {
	case nil:
		setNone(c, path) // None：key 存在、body 空（JSON null）
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

// writeContainer：容器值落裸 base（body 空）。dims 空 = 命名字典（对象）；dims=[n] = 坐标数组。
// storetype 恒 index（spec map容器）；langtype 由 codec 产出（裸 stringkeymap）。
func writeContainer(c unsafe.Pointer, path string, dims []int32) {
	tlv := encodeTLV(kindMap, nil, dims)
	h, ok := headOf(tlv)
	if !ok {
		return
	}
	writeBody(c, path, 0, storetypeIndex, 0, 0, langtypeOf(&h), nil)
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
	writeContainer(c, path, nil)
	for _, k := range keys {
		if err := writeValue(c, path+sep+k, m[k]); err != nil {
			return err
		}
	}
	return nil
}

func writeArr(c unsafe.Pointer, path string, arr []interface{}) error {
	writeContainer(c, path, []int32{int32(len(arr))})
	for i, v := range arr {
		if err := writeValue(c, path+sep+fmt.Sprintf("[%d]", i), v); err != nil {
			return err
		}
	}
	return nil
}

// isContainer：容器值在 p（body 空，map langtype），或 memindex 落 p·（成员已写）。
func isContainer(c unsafe.Pointer, path string) bool {
	if h, ok := headAt(c, path); ok && h.ref == 0 && isMapLangtype(langtypeOf(&h)) {
		return true
	}
	h, ok := headAt(c, path+sep)
	return ok && (h.storetype == storetypeIndex || h.storetype == storetypeExtIndex)
}

// isDir：p/ 是 index/extindex 目录（层级目录，与 · 成员目录并列）。
func isDir(c unsafe.Pointer, path string) bool {
	h, ok := headAt(c, path+dirSuf)
	return ok && (h.storetype == storetypeIndex || h.storetype == storetypeExtIndex)
}

func readValue(c unsafe.Pointer, path string) interface{} {
	if path != "/" {
		if isContainer(c, path) {
			return readContainer(c, path)
		}
	}
	if isDir(c, path) {
		return readDir(c, path)
	}
	kind, raw, arrLen := parseTLV(getTLV(c, path, 0))
	if kind == "" {
		return nil // None → JSON null
	}
	return tlvToJSONValue(kind, raw, arrLen)
}

// readDir：/ 目录树 → JSON object；子名带尾 /（子目录）或 ·（成员目录）先 strip。
func readDir(c unsafe.Pointer, path string) map[string]any {
	m := map[string]any{}
	for _, name := range list(c, dirPrefix(path), 0) {
		key := strings.TrimSuffix(strings.TrimSuffix(name, dirSuf), sep)
		m[key] = readValue(c, childPath(path, key))
	}
	return m
}

// memberNames：p· 的成员名。尾 `·` 项是成员的 memindex 目录（fs 后端按原始目录项列出），
// 成员名本身禁 `·`（spec 成员名字符约束），故剥尾 `·` 并按名字去重。
func memberNames(c unsafe.Pointer, prefix string) []string {
	var names []string
	seen := map[string]bool{}
	for _, n := range list(c, prefix, 0) {
		n = strings.TrimSuffix(n, sep)
		if n == "" || seen[n] {
			continue
		}
		seen[n] = true
		names = append(names, n)
	}
	return names
}

// readContainer：p· 成员枚举 → 数组或对象。
func readContainer(c unsafe.Pointer, path string) interface{} {
	names := memberNames(c, path+sep)
	if len(names) == 0 {
		if h, ok := headAt(c, path); ok && h.ndim >= 1 {
			return []interface{}{} // 数组：容器值带 dims=[n]
		}
		return map[string]any{}
	}
	allIdx := true
	for _, n := range names {
		if _, ok := coordIndex(n); !ok {
			allIdx = false
			break
		}
	}
	if allIdx {
		return readArr(c, path, names)
	}
	return readObj(c, path, names)
}

func readObj(c unsafe.Pointer, path string, names []string) map[string]any {
	m := map[string]any{}
	for _, name := range names {
		m[name] = readValue(c, path+sep+name)
	}
	return m
}

func readArr(c unsafe.Pointer, path string, names []string) []interface{} {
	idxs := make([]int, 0, len(names))
	for _, n := range names {
		if i, ok := coordIndex(n); ok {
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

func dirPrefix(path string) string {
	if path == "" || path == "/" {
		return dirSuf
	}
	return path + dirSuf
}

func childPath(path, name string) string {
	if path == "" || path == "/" {
		return dirSuf + name
	}
	return path + dirSuf + name
}

// ptrTarget：路径上是 Ptr（ref=1）时取其 body（目标 key），否则原样返回。
func ptrTarget(c unsafe.Pointer, path string) string {
	data := getTLV(c, path, 0)
	h, ok := headOf(data)
	if !ok || h.ref != refPtr {
		return path
	}
	bo, bl := int(h.body_offset), int(h.body_len)
	if bo < 0 || bl < 0 || bo+bl > len(data) {
		return path
	}
	return string(data[bo : bo+bl])
}

// rootOf：读参 idx 解析为可遍历的绝对路径（变量 → 帧槽路径，Ptr → 其目标）。
func rootOf(c unsafe.Pointer, pc string, name string, idx int) string {
	root := name
	if !strings.HasPrefix(root, "/") {
		root = resolveReadPath(c, pc, idx)
	}
	if root == "" {
		return ""
	}
	return ptrTarget(c, root)
}

// write：顶层写入（覆盖语义，root 子树等于 src）。v 可为 map/slice/标量/nil。
func write(c unsafe.Pointer, root string, v interface{}) error {
	if root != "" && root != "/" {
		delTree(c, root)
	}
	return writeValue(c, root, v)
}

func build(c unsafe.Pointer, root string) interface{} { return readValue(c, root) }

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

func cstrArr(n int) (**C.char, []*C.char) {
	if n == 0 {
		return nil, nil
	}
	arr := make([]*C.char, n)
	for i := range arr {
		arr[i] = cstr("any")
	}
	return (**C.char)(unsafe.Pointer(&arr[0])), arr
}

func register(c unsafe.Pointer) {
	for _, o := range myrwircaps {
		co := cstr(o.name)
		rp, ra := cstrArr(o.nr)
		wp, wa := cstrArr(o.nw)
		C.kvlangDefRwir(c, co, rp, C.int32_t(o.nr), wp, C.int32_t(o.nw))
		C.free(unsafe.Pointer(co))
		for _, p := range ra {
			C.free(unsafe.Pointer(p))
		}
		for _, p := range wa {
			C.free(unsafe.Pointer(p))
		}
	}
}

func doTo(c unsafe.Pointer, pc string, readNames []string) {
	root := rootOf(c, pc, readNames[0], 0)
	if root == "" {
		return
	}
	data, _ := json.Marshal(build(c, root))
	setTLV(c, resolveWrite(c, pc, 0), constructChar(data))
}

func fail(c unsafe.Pointer, vid string, msg string) {
	setNone(c, "/vthread/"+vid+"/"+runtimeSep+"error")
	setTLV(c, "/vthread/"+vid+"/"+runtimeSep+"error/msg", constructChar([]byte(msg)))
	setTLV(c, "/vthread/"+vid+"/"+runtimeSep+"status", constructChar([]byte("error")))
}

func doFrom(c unsafe.Pointer, pc string, writeNames []string, vid string) {
	src := resolveRead(c, pc, 0)
	root := writeNames[0]
	if !strings.HasPrefix(root, "/") {
		root = resolveWrite(c, pc, 0)
	}
	v, err := fromJSON([]byte(src))
	if err != nil {
		fail(c, vid, err.Error())
		return
	}
	if err := write(c, root, v); err != nil {
		fail(c, vid, err.Error())
	}
}

// specOf：opcode → 本扩展的 op 签名（非本扩展的 rwir 返回 false）。
func specOf(name string) (op, bool) {
	for _, o := range myrwircaps {
		if o.name == name {
			return o, true
		}
	}
	return op{}, false
}

// serveOp：认领 handoff 条目（/lib/<op>/vids/<vid>，值为 pc），执行后推进 vthread PC 并删除该
// 条目（runtime 端 watch 该键直至变 None）。各 rwir 的 vids 队列被 Ptr 统一到最早注册者，故按
// params 首行的 opcode 分派——他 runtime 的条目一律不认领、不删除。
func serveOp(c unsafe.Pointer) {
	claimed := map[string]bool{}
	for _, o := range myrwircaps {
		for _, vid := range list(c, "/lib/"+o.name+"/vids/", 1) {
			if claimed[vid] {
				continue
			}
			claimed[vid] = true
			pc := get(c, "/lib/"+o.name+"/vids/"+vid, 1)
			if pc == "" {
				continue // 已被其它进程认领
			}
			ps := params(c, pc)
			spec, mine := specOf(ps[0])
			if !mine || len(ps) < 1+spec.nr+spec.nw {
				continue
			}
			key := "/lib/" + spec.name + "/vids/" + vid
			if spec.name == "json"+sep+"to" {
				doTo(c, pc, ps[1:1+spec.nr])
			} else {
				doFrom(c, pc, ps[1+spec.nr:1+spec.nr+spec.nw], vid)
			}
			setTLV(c, "/vthread/"+vid+"/"+runtimeSep+"pc", constructChar([]byte(nextPC(pc))))
			del(c, key)
		}
	}
}

func connect(dsn string) unsafe.Pointer {
	cd := cstr(dsn)
	defer C.free(unsafe.Pointer(cd))
	return C.kvspaceConnect(cd)
}

func disconnect(c unsafe.Pointer) { C.kvspaceClose(c) }

// Serve 常驻循环：注册 + 认领 handoff 条目 + 批量执行 + 交还 PC。
func Serve(dsn string) {
	c := connect(dsn)
	if c == nil {
		return
	}
	defer C.kvspaceClose(c)
	register(c)
	for {
		serveOp(c)
		time.Sleep(20 * time.Millisecond)
	}
}

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* ── kvspace-durable C ABI ─────────────────────────────────────────── */

typedef struct {
    uint8_t kind[32];
    uint8_t is_ptr;
    int32_t array_len;
    int32_t body_len;
    int32_t body_offset;
} kvhead_t;

extern void *kvspace_conn(const char *dsn);
extern void  kvspace_free(void *h);
extern void  kvspace_bytes_free(uint8_t *p, uint32_t len);
extern int   kvspace_set(void *h, const char *const *keys, const uint8_t *vals,
                         const uint32_t *lens, uint32_t n, char *err, uint32_t err_cap);
extern int   kvspace_get_one(void *h, const char *key, uint8_t **out, uint32_t *out_len);
extern int   kvspace_get_batch(void *h, const char *prefix, const char *const *names,
                               uint32_t nnames, uint8_t **out, uint32_t *out_len);
extern int   kvspace_list(void *h, const char *prefix, int expand_ext, int resolve,
                          uint8_t **out, uint32_t *out_len);
extern int   kvspace_del(void *h, const char *const *keys, uint32_t nkeys, char *err, uint32_t err_cap);
extern int   kvspace_del_tree(void *h, const char *prefix, char *err, uint32_t err_cap);
extern int   kvspace_mkindex(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspace_ext_index(void *h, const char *path, const char *ext_path, char *err, uint32_t err_cap);
extern int   kvspace_del_ext_index(void *h, const char *path, char *err, uint32_t err_cap);
extern int   kvspace_watch(void *h, const char *key, const uint8_t *target, uint32_t target_len,
                           uint64_t tick_ns, uint8_t **out, uint32_t *out_len);
extern int   kvspace_tlv_encode(const char *kind, const uint8_t *raw, uint32_t raw_len,
                                int32_t array_len, uint8_t **out, uint32_t *out_len);
extern int   kvspace_tlv_encode_ptr(const char *kind, const uint8_t *raw, uint32_t raw_len,
                                    int32_t array_len, uint8_t **out, uint32_t *out_len);
extern int   kvspace_decode_head(const uint8_t *data, uint32_t data_len, kvhead_t *out);
extern int   kvspace_new_ptr(const char *kind, const char *target, int32_t array_len,
                             uint8_t **out, uint32_t *out_len);
extern int   kvspace_new_char(const char *kind, const char *s, uint8_t **out, uint32_t *out_len);
extern int   kvspace_new_char_byte(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len);
extern int   kvspace_new_bool(uint8_t v, uint8_t **out, uint32_t *out_len);
extern int   kvspace_new_int64(int64_t v, uint8_t **out, uint32_t *out_len);
extern int   kvspace_new_float64(double v, uint8_t **out, uint32_t *out_len);

/* ── kind 常量 ─────────────────────────────────────────────────────── */

#define K_NONE       "None"
#define K_BOOL       "bool"
#define K_INT8       "int8"
#define K_INT16      "int16"
#define K_INT32      "int32"
#define K_INT64      "int64"
#define K_UINT8      "uint8"
#define K_UINT16     "uint16"
#define K_UINT32     "uint32"
#define K_UINT64     "uint64"
#define K_FLOAT32    "float32"
#define K_FLOAT64    "float64"
#define K_CHAR       "char/utf32"
#define K_CHAR_UTF8  "char/utf8"
#define K_CHAR_ASCII "char/ascii"
#define K_DICT       "dict"
#define K_INDEX      "index"
#define K_EXT_INDEX  "extindex"
#define K_RWIR       "rwir"
#define K_RWFUNC     "rwfunc"
#define K_SCOPE      "scope"
#define K_TIME       "time"
#define K_DURATION   "duration"

/* ── 路径/成员常量 ─────────────────────────────────────────────────── */

#define PATH_SEP          "/"
#define DIR_INDEX_SUF     "/"
#define MEMBER_SEP        "."
#define INDEX_VALUE_SEP   "\n"
#define RUNTIME_MEMBER_SEP "\xE2\x80\xA5"   /* ‥ U+2025 */
#define EXT_INDEX_HEAD    "\xE2\x80\xA6"    /* … U+2026 */

#define MAX_PARAMS 128
#define MAX_STACK_DEPTH 256

/* ── 基础类型 ──────────────────────────────────────────────────────── */

typedef struct { uint8_t *data; uint32_t len; } xval_t;

typedef struct { char *key; xval_t val; } kv_pair_t;

typedef struct { void *h; } kv_t;

/* growable string buffer */
typedef struct { char *p; size_t len, cap; } sbuf_t;

static inline void sb_init(sbuf_t *b) { b->p = NULL; b->len = 0; b->cap = 0; }
void sb_putc(sbuf_t *b, char c);
void sb_putn(sbuf_t *b, const char *s, size_t n);
static inline void sb_puts(sbuf_t *b, const char *s) { sb_putn(b, s, strlen(s)); }
void sb_printf(sbuf_t *b, const char *fmt, ...);
char *sb_detach(sbuf_t *b);      /* malloc，调用方 free */
static inline void sb_free(sbuf_t *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

/* ── XValue 操作 ───────────────────────────────────────────────────── */

static inline bool xv_none(const xval_t *v) { return v->data == NULL || v->len == 0; }
static inline void xv_zero(xval_t *v) { v->data = NULL; v->len = 0; }
void xv_free(xval_t *v);          /* kvspace_bytes_free */
void xv_set_bytes(xval_t *v, uint8_t *data, uint32_t len);  /* 接管内存 */
int  xv_head(const xval_t *v, kvhead_t *h);                 /* decode head */
const char *xv_kind(const xval_t *v);                       /* 返回 kind，None="" */
bool xv_kind_is(const xval_t *v, const char *kind);
bool xv_is_ptr(const xval_t *v);
int32_t xv_array_len(const xval_t *v);
const uint8_t *xv_body(const xval_t *v, const kvhead_t *h, int32_t *out_len);
char *xv_ptr_target(const xval_t *v);                       /* malloc */
char *xv_value_string(const xval_t *v);                     /* malloc，对齐 Go ValueString */
bool xv_is_char_kind(const char *kind);
bool xv_is_int_kind(const char *kind);
bool xv_is_uint_kind(const char *kind);
bool xv_is_float_kind(const char *kind);
bool xv_is_num_kind(const char *kind);
int64_t xv_as_int64(const xval_t *v);
double  xv_as_float64(const xval_t *v);
uint64_t xv_as_uint64(const xval_t *v);
bool    xv_as_bool(const xval_t *v);
uint32_t xv_char32_at(const xval_t *v, int32_t idx);
int32_t xv_elem_size(const char *kind);

void xv_new_int64(xval_t *v, int64_t n);
void xv_new_float64(xval_t *v, double f);
void xv_new_bool(xval_t *v, bool b);
void xv_new_char_utf8(xval_t *v, const char *s);
void xv_new_char_utf32(xval_t *v, const char *s);  /* UTF-8 → UTF-32 LE body */
void xv_new_char_kind(xval_t *v, const char *kind, const char *s);
void xv_new_ptr(xval_t *v, const char *kind, const char *target, int32_t al);
void xv_new_rwir(xval_t *v, int32_t nr, int32_t nw, const char *sig);
void xv_new_tlv(xval_t *v, const char *kind, const uint8_t *raw, uint32_t raw_len, int32_t al);

void fmt_float(char *out, size_t cap, double v);

/* ── KV 操作（封装 durable ABI）────────────────────────────────────── */

kv_t *kv_connect(const char *dsn);
void kv_disconnect(kv_t *k);
int kv_get_one(kv_t *k, const char *key, xval_t *out);   /* None → out len=0 */
int kv_get_batch(kv_t *k, const char *prefix, char **names, int n, xval_t *out);
int kv_set(kv_t *k, const kv_pair_t *pairs, int n, char *err, uint32_t err_cap);
int kv_del(kv_t *k, const char *key, char *err, uint32_t err_cap);
int kv_del_tree(kv_t *k, const char *prefix, char *err, uint32_t err_cap);
int kv_mkindex(kv_t *k, const char *path, char *err, uint32_t err_cap);
int kv_ext_index(kv_t *k, const char *path, const char *ext, char *err, uint32_t err_cap);
int kv_del_ext_index(kv_t *k, const char *path, char *err, uint32_t err_cap);
int kv_list(kv_t *k, const char *prefix, bool expand_ext, bool resolve,
            char ***out_names, int *out_count);           /* split \n */
int kv_watch(kv_t *k, const char *key, const xval_t *target, uint64_t tick_ns, xval_t *out);

/* ── keytree ───────────────────────────────────────────────────────── */

#define SEG_LIB     RUNTIME_MEMBER_SEP "lib"
#define SEG_PC      "pc"
#define SEG_STATUS  "status"
#define SEG_CALLPC  "callpc"
#define SEG_RETURNPC "returnpc"
#define SEG_RO      "ro"
#define SEG_MSG     "msg"
#define LIB_ROOT    "/lib"
#define VTHREAD_ROOT "/vthread"

static inline void sb_clear(sbuf_t *b) { b->len = 0; if (b->p) b->p[0] = 0; }

const char *kt_vtid_from_pc(const char *pc, sbuf_t *out);   /* "" 无效 */
char *kt_stack(const char *root);                            /* malloc */
char *kt_frame_root(const char *pc);                         /* malloc，无效 NULL */
char *kt_entry_pc(const char *root);                         /* malloc */
char *kt_scope_entry_pc(const char *root);                   /* malloc */
char *kt_parent_frame(const char *root);                     /* malloc，"" 顶层 */
char *kt_member(const char *base, const char *name);         /* malloc */
char *kt_lib_func(const char *pkg, const char *name);        /* malloc */
char *kt_rwir(const char *opcode);                           /* malloc */
void kt_vthread(const char *vtid, sbuf_t *out);
void kt_vthread_slot(const char *vtid, const char *frame, int i, int j, sbuf_t *out);
void kt_vthread_pc(const char *vtid, sbuf_t *out);
void kt_vthread_status(const char *vtid, sbuf_t *out);
void kt_vthread_status_msg(const char *vtid, const char *status, sbuf_t *out);
void kt_vthread_debugger(const char *vtid, sbuf_t *out);
void kt_frame_callpc(const char *root, sbuf_t *out);
void kt_frame_returnpc(const char *root, sbuf_t *out);
void kt_frame_ro(const char *root, sbuf_t *out);
bool kt_is_entry_pc(const char *pc);

/* ── rwir ──────────────────────────────────────────────────────────── */

#define OP_CALL   "call"
#define OP_RETURN "return"
#define OP_BR     "br"
#define OP_GOTO   "goto"
#define OP_ASSIGN "assign"

static inline bool op_is_control(const char *op) {
    return strcmp(op, OP_CALL) == 0 || strcmp(op, OP_RETURN) == 0 ||
           strcmp(op, OP_BR) == 0 || strcmp(op, OP_GOTO) == 0;
}

typedef struct { char *name; xval_t val; } param_t;

typedef struct {
    char *opcode;
    param_t *reads; int nr;
    param_t *writes; int nw;
} rwir_inst_t;

int rwir_next_pc(const char *pc, sbuf_t *out);
int rwir_extract_addr0(const char *coord);
int rwir_decode(kv_t *kv, const char *link_base, const char *pc, rwir_inst_t *out, char *err, uint32_t err_cap);
void rwir_inst_free(rwir_inst_t *inst);

/* ── vthread ───────────────────────────────────────────────────────── */

void vt_get(kv_t *kv, const char *vtid, char **pc, char **status);
void vt_set(kv_t *kv, const char *vtid, const char *pc, const char *status);
void vt_set_done(kv_t *kv, const char *vtid, const char *ret);
void vt_set_error(kv_t *kv, const char *vtid, const char *pc, const char *msg);

/* ── builtin ───────────────────────────────────────────────────────── */

typedef struct { kv_t *kv; const char *vtid; const char *pc; rwir_inst_t *inst; } frame_t;

bool bi_is_native(const char *opcode);
bool bi_num_op(const char *opcode);
int bi_native(frame_t *f);   /* dispatch + call，0 成功 */
int bi_execute_copy(kv_t *kv, const char *vtid, const char *pc, rwir_inst_t *inst);
void bi_resolve_read_value(kv_t *kv, const char *frame_path, const char *name,
                           const xval_t *val, xval_t *out);
char *bi_resolve_write_slot(kv_t *kv, const char *frame_path, const char *name);
char *bi_func_frame_root(kv_t *kv, const char *frame_root);   /* malloc */
bool bi_try_parse_number(const char *s, xval_t *out);          /* 成功 out 接管 */
void display(const xval_t *v, char **out);                     /* malloc，对齐 Go Display */

/* ── kvcpu ─────────────────────────────────────────────────────────── */

int kvcpu_execute(kv_t *kv, const char *pc);
char *kvcpu_bootstrap(kv_t *kv, const char *vtid, const char *funcname, const char *const *args, int nargs);

/* ── logx ──────────────────────────────────────────────────────────── */

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);

/* #503 P2：32-bit 寄存器字节码（runtime-c 侧接口，P0 冻结）。
 * 格式与字段见 workspace/499-p0-bytecode-spec.md §1.3/§1.4；
 * layout 侧（coding3）写同一 wire 格式。这里只做解析/校验/缓存。 */
#ifndef KVLANG_KVCODE_H
#define KVLANG_KVCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KVCODE_MAGIC 0x4342564bU /* "KVBC" little-endian */
#define KVCODE_VERSION 1u
#define KV_REG_NONE UINT64_MAX
#define KVCODE_NO_CHECKPOINT 0xFFFFu

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t ninsn;
    uint16_t nregs; /* <= 128：B/C 只有 7 bit 寄存器 */
    uint16_t nconst;
    uint16_t nslot;
    uint16_t ncomplex;
    uint16_t ncheckpoint;
    uint16_t ninit;
    char isa_hash[33];
    char compact_hash[33];
    uint32_t runtime_version;
    uint64_t body_hash;
    uint64_t code_hash;
    uint32_t off_insn, off_const, off_slot, off_complex, off_checkpoint,
        off_pc_map, off_init;
    uint32_t off_strings; /* string table（slot 路径模板 / checkpoint pc） */
    uint32_t strings_len; /* 含结尾 NUL */
    uint32_t total_size;
} kvlangCodeHeader;

typedef struct {
    uint64_t addr; /* 预创建 XValue 的 kvspace 64-bit 地址 */
    uint8_t kind;
    uint8_t flags;
    uint16_t pad;
} kvlangConst;

typedef struct {
    uint16_t slot_id;
    uint8_t kind;
    uint8_t storetype;
    uint32_t width;
    uint32_t cap;
    uint32_t path_off; /* 槽路径模板在 string table 的偏移 */
} kvlangSlot;

typedef struct {
    uint16_t compact_id;
    uint8_t resp;
    uint8_t nr, nw;
    uint8_t flags; /* handoff | native | creation */
} kvlangComplex;

typedef struct {
    uint32_t insn;
    uint32_t kv_pc_off;
    uint8_t kind; /* entry/exit/call/return/branch/periodic */
    uint8_t flags;
    uint16_t pad;
} kvlangCheckpoint;

typedef struct {
    uint16_t slot_id;
    uint16_t kind_id;
    uint32_t width;
    uint32_t cap;
    uint8_t flags; /* scalar|string|array|ndarray|container */
} kvlangInitSlot;

typedef struct kvlangCode {
    kvlangCodeHeader h;
    const uint32_t *insn;
    const kvlangConst *consts;
    const kvlangSlot *slots;
    const kvlangComplex *complex;
    const kvlangCheckpoint *checkpoints;
    const uint16_t *pc_map;
    const kvlangInitSlot *init;
    const char *strings;
    uint8_t *blob; /* owned：字节码副本（kvspace 值可能被后端移动） */
    uint32_t blob_len;
} kvlangCode;

/* 寄存器只存地址；不缓存值（#499 裁决）。 */
typedef struct {
    uint64_t addr; /* KV_REG_NONE = None */
    uint32_t gen;
    uint8_t kind;
    uint8_t flags;
} kvlangReg;

typedef struct {
    kvlangReg *regs;
    uint16_t nregs;
    uint32_t epoch;
} kvlangRegFile;

uint64_t kvlangCodeHash(const uint8_t *buf, uint32_t len);
int kvlangCodeParse(const uint8_t *buf, uint32_t len, kvlangCode *out,
                    char *err, size_t err_cap);
kvlangCode *kvlangCodeLoad(void *kvspace, const char *func_path);
void kvlangCodeFree(kvlangCode *c);
void kvlangCodeCacheReset(void);

void kvlangRegFileInit(kvlangRegFile *rf, uint16_t nregs);
void kvlangRegFileFree(kvlangRegFile *rf);
void kvlangRegSet(kvlangRegFile *rf, uint16_t r, uint64_t addr, uint8_t kind);
void kvlangRegSetNone(kvlangRegFile *rf, uint16_t r);
void kvlangRegInvalidate(kvlangRegFile *rf, uint32_t epoch);

#endif

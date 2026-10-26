#include "kvcode.h"
#include "runtime_internal.h"

#include <unistd.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    exit(1);
}

static void expect(bool ok, const char *msg) {
    if (!ok)
        fail(msg);
}

/* 合成一个合法 Code blob：2 insn、1 const、1 slot、1 complex、1 checkpoint、
 * 1 init、1 条 pc_map、string table 两条。 */
typedef struct {
    uint8_t buf[4096];
    uint32_t n;
} img;

static uint32_t put(img *im, const void *p, uint32_t len) {
    while (im->n & 7u)
        im->buf[im->n++] = 0;
    uint32_t off = im->n;
    memcpy(im->buf + off, p, len);
    im->n += len;
    return off;
}

static void build(img *im) {
    memset(im, 0, sizeof *im);
    im->n = sizeof(kvlangCodeHeader);
    uint32_t insn[2] = {0x01020304u, 0x05060708u};
    kvlangConst k = {0x1000, 7, 0, 0};
    kvlangSlot s = {0, 7, 1, 40, 0, 0};
    kvlangComplex cx = {42, 1, 2, 1, 1};
    kvlangCheckpoint cp = {1, 0, 1, 0, 0};
    kvlangInitSlot in = {0, 7, 40, 40, 1};
    uint16_t pc_map[2] = {0, KVCODE_NO_CHECKPOINT};
    uint32_t o_insn = put(im, insn, sizeof insn);
    uint32_t o_const = put(im, &k, sizeof k);
    uint32_t o_slot = put(im, &s, sizeof s);
    uint32_t o_cx = put(im, &cx, sizeof cx);
    uint32_t o_cp = put(im, &cp, sizeof cp);
    uint32_t o_pc = put(im, pc_map, sizeof pc_map);
    uint32_t o_in = put(im, &in, sizeof in);
    const char strings[] = "/vthread/demo/a\0/‥pc\0";
    uint32_t o_str = put(im, strings, sizeof strings);
    kvlangCodeHeader h;
    memset(&h, 0, sizeof h);
    h.magic = KVCODE_MAGIC;
    h.version = KVCODE_VERSION;
    h.ninsn = 2;
    h.nregs = 8;
    h.nconst = 1;
    h.nslot = 1;
    h.ncomplex = 1;
    h.ncheckpoint = 1;
    h.ninit = 1;
    snprintf(h.isa_hash, sizeof h.isa_hash, "corehash");
    snprintf(h.compact_hash, sizeof h.compact_hash, "compact");
    h.off_insn = o_insn;
    h.off_const = o_const;
    h.off_slot = o_slot;
    h.off_complex = o_cx;
    h.off_checkpoint = o_cp;
    h.off_pc_map = o_pc;
    h.off_init = o_in;
    h.off_strings = o_str;
    h.strings_len = sizeof strings;
    h.total_size = im->n;
    memcpy(im->buf, &h, sizeof h);
    kvlangCodeHeader *hp = (kvlangCodeHeader *)im->buf;
    hp->code_hash = kvlangCodeHash(im->buf, hp->total_size);
}

static void check_parsed(img *im) {
    kvlangCode c;
    char err[256] = {0};
    expect(kvlangCodeParse(im->buf, im->n, &c, err, sizeof err) == 0, err);
    expect(c.h.ninsn == 2 && c.h.nregs == 8, "header counts");
    expect(c.insn && c.insn[0] == 0x01020304u, "insn view");
    expect(c.consts && c.consts[0].addr == 0x1000, "const view");
    expect(c.slots && c.slots[0].width == 40, "slot view");
    expect(c.complex && c.complex[0].compact_id == 42, "complex view");
    expect(c.checkpoints && c.checkpoints[0].insn == 1, "checkpoint view");
    expect(c.pc_map[0] == 0 && c.pc_map[1] == KVCODE_NO_CHECKPOINT,
           "pc_map view");
    expect(c.init && c.init[0].cap == 40, "init view");
    expect(c.strings && strcmp(c.strings, "/vthread/demo/a") == 0,
           "string table");
}

int main(void) {
    img im;
    build(&im);
    check_parsed(&im);

    /* magic */
    img bad = im;
    ((kvlangCodeHeader *)bad.buf)->magic = 0xdeadbeef;
    kvlangCode c;
    char err[256];
    expect(kvlangCodeParse(bad.buf, bad.n, &c, err, sizeof err) != 0, "magic");

    /* hash */
    bad = im;
    bad.buf[bad.n - 1] ^= 0xff;
    expect(kvlangCodeParse(bad.buf, bad.n, &c, err, sizeof err) != 0,
           "hash mismatch");

    /* bad section offset */
    bad = im;
    ((kvlangCodeHeader *)bad.buf)->off_insn = 3;
    ((kvlangCodeHeader *)bad.buf)->code_hash =
        kvlangCodeHash(bad.buf, bad.n);
    expect(kvlangCodeParse(bad.buf, bad.n, &c, err, sizeof err) != 0,
           "bad offset");

    /* bad slot path_off */
    bad = im;
    {
        kvlangSlot *s = (kvlangSlot *)(bad.buf +
                                       ((kvlangCodeHeader *)bad.buf)->off_slot);
        s->path_off = 0xFFFF;
        ((kvlangCodeHeader *)bad.buf)->code_hash =
            kvlangCodeHash(bad.buf, bad.n);
    }
    expect(kvlangCodeParse(bad.buf, bad.n, &c, err, sizeof err) != 0,
           "bad path_off");

    /* register file */
    kvlangRegFile rf;
    kvlangRegFileInit(&rf, 4);
    expect(rf.nregs == 4 && rf.regs[0].addr == KV_REG_NONE, "regfile init");
    kvlangRegSet(&rf, 1, 0x1234, 7);
    expect(rf.regs[1].addr == 0x1234 && rf.regs[1].kind == 7, "reg set");
    kvlangRegInvalidate(&rf, 9);
    expect(rf.epoch == 9 && rf.regs[1].gen == 9, "reg invalidate");
    kvlangRegSetNone(&rf, 1);
    expect(rf.regs[1].addr == KV_REG_NONE, "reg none");
    kvlangRegFileFree(&rf);

    /* loader via kvspace + cache */
    char dsn[128];
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang-kvcode-%d", (int)getpid());
    kvspace_t *kv = kvlangKvspaceConnect(dsn);
    if (!kv)
        fail("connect");
    char key[KV_KEY_MAX];
    snprintf(key, sizeof key, "/lib/demo" RUNTIME_MEMBER_SEP "f/" RUNTIME_MEMBER_SEP "code");
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNew(&v, "uint8", im.buf, im.n, (int32_t)im.n);
    kvlangKvPair_t p = {key, v};
    char serr[256];
    expect(kvlangKvspaceSet(kv, &p, 1, serr, sizeof serr) == 0, "set code");
    kvlangXvalueFree(&v);
    kvlangCodeCacheReset();
    kvlangCode *a = kvlangCodeLoad(kv, "/lib/demo" RUNTIME_MEMBER_SEP "f");
    kvlangCode *b = kvlangCodeLoad(kv, "/lib/demo" RUNTIME_MEMBER_SEP "f");
    expect(a && a == b, "cache hit");
    expect(a->insn[1] == 0x05060708u, "loaded insn");
    kvlangCodeCacheReset();
    kvlangKvspaceDisconnect(kv);
    printf("OK test_kvcode\n");
    return 0;
}

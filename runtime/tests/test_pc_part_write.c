/* PC 槽等价值写（C-i）：shm 走 part-write `[2, 64+a)`，其它后端整值写。
 * 覆盖长度边界（9↔10、39↔41、99↔100、255↔257）与跨 key 切换：
 * 每次写入后 Get/GetHead/GetPart 与正典 slack codec 的 [0,64+a) 逐字节一致。 */

#include "runtime_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                      \
    } while (0)

/* 生成恰好 len 字节的 ASCII PC（内容无语义，只测编码长度）。 */
static void mkpc(char *out, size_t len) {
    static const char *pre = "/vthread/1/[";
    size_t p = strlen(pre);
    memcpy(out, pre, p);
    for (size_t i = p; i + 1 < len; i++)
        out[i] = (char)('0' + (i % 10));
    out[len - 1] = ']';
    out[len] = 0;
}

static void check_slot(kvspace_t *kv, const char *key, const char *s,
                       bool strict_tail) {
    uint32_t len = (uint32_t)strlen(s);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    CHECK(kvlangKvspaceGetOne(kv, key, &v) == 0);
    kvspaceHead_t h;
    CHECK(kvspaceDecodeHead(v.bytes, v.length, &h) == 0);
    CHECK(h.storetype == 1 /* slack */);
    CHECK(h.body_len == (int32_t)len);
    CHECK(h.body_cap >= (uint64_t)len);
    uint8_t *ref = NULL;
    uint64_t rl = 0;
    CHECK(kvspaceXhNewSlack(1 /* UTF-8 */, (const uint8_t *)s, len, h.body_cap,
                            &ref, &rl) == 0);
    CHECK(v.length == (uint32_t)rl);
    size_t prefix = 64 + len;
    CHECK(prefix <= (size_t)rl && memcmp(v.bytes, ref, prefix) == 0);
    if (strict_tail) /* 整值写：尾部零填充也一致；part-write 的 b-a 不属于值 */
        CHECK(memcmp(v.bytes, ref, (size_t)rl) == 0);
    uint8_t *d = NULL;
    uint32_t got = 0;
    CHECK(kvspaceGetPart(kv->handle, key, 64, len, &d, &got) == 0 &&
          got == len && memcmp(d, s, len) == 0);
    free(ref);
}

static void write_pc(kvspace_t *kv, kvspaceKeyOff *ko, const char *s) {
    kvlangFrame_t f;
    memset(&f, 0, sizeof f);
    f.kvspace = kv;
    f.vthread_id = "t";
    f.pc_off = ko;
    f.status_known = "running"; /* 状态未变：不写 status 槽 */
    kvlangVthreadNextPc(&f, s, "running");
    CHECK(!f.persist_failed);
}

int main(void) {
    char dir[] = "/tmp/kvlang-pc-part-XXXXXX";
    char dsn[256];
    if (!mkdtemp(dir))
        return 1;
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    bool shm = strncmp(dsn, "shm://", 6) == 0;
    kvspace_t *kv = kvlangKvspaceConnect(dsn);
    if (!kv)
        return 2;
    char key[KV_KEY_MAX], key_b[KV_KEY_MAX];
    kvlangKeytreeVthreadPc("a", key, sizeof key);
    kvlangKeytreeVthreadPc("b", key_b, sizeof key_b);
    kvspaceKeyOff ko, ko_b;
    kvspaceKeyOffBind(&ko, kv, key);
    kvspaceKeyOffBind(&ko_b, kv, key_b);

    static const size_t lens[] = {8,   9,   10,  9,   39,  40,  41,  99,
                                  100, 101, 99,  255, 256, 257, 20,  9};
    char s[KV_KEY_MAX], sb[KV_KEY_MAX];
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        mkpc(s, lens[i]);
        write_pc(kv, &ko, s);
        check_slot(kv, key, s, !shm);
        /* 跨 key 切换后回到原 key：缓存必须整体重建，不得串味。 */
        mkpc(sb, 16);
        write_pc(kv, &ko_b, sb);
        check_slot(kv, key_b, sb, !shm);
        write_pc(kv, &ko, s);
        check_slot(kv, key, s, !shm);
    }
    kvlangKvspaceDisconnect(kv);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("ok: pc part-write (%s)\n", scheme ? scheme : "shm");
    return 0;
}

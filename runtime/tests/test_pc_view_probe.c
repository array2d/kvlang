/* b1b：读取 /vthread/<vid>/‥pc 的 xvalue body view 并打印字段，供 shell 断言。 */
#include <kvspace/kvspace.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint64_t rd64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static uint64_t rd64(const uint8_t *p);
static void wr64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* shrink：把 ‥pc 重写为 langtype 基类型不符的槽（模拟 owned 后不合规），
 * 用于验证 runtime 回退仍能正确写 PC。 */
static int shrink(void *h, const char *key) {
    uint8_t *val = NULL;
    uint32_t vlen = 0;
    if (kvspaceGet(h, key, 0, &val, &vlen) != 0 || !val || vlen < 64)
        return 1;
    uint64_t a = rd64(val + 2);
    if (64 + a > vlen)
        return 1;
    char lt[64];
    int n = snprintf(lt, sizeof lt, "[%llu]char/ascii", (unsigned long long)a);
    if (n <= 0 || n >= 60)
        return 1;
    uint8_t *body = NULL;
    char err[128];
    uint8_t saved[1024];
    if (a >= sizeof saved)
        return 1;
    memcpy(saved, val + 64, (size_t)a); /* WriteNewPlace 可能移动/释放旧 box */
    if (kvspaceWriteNewPlace(h, key, 0, 1, 0, 0, lt, (uint32_t)a, 1024, &body,
                             err, sizeof err) != 0 ||
        !body)
        return 1;
    memcpy(body, saved, (size_t)a);
    printf("shrunk a=%llu langtype=%s\n", (unsigned long long)a, lt);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    void *h = kvspaceConnect(argv[1]);
    if (!h)
        return 1;
    char key[256];
    snprintf(key, sizeof key, "/vthread/%s/\xE2\x80\xA5pc", argv[2]);
    if (argc >= 4 && strcmp(argv[3], "shrink") == 0) {
        int rc = shrink(h, key);
        kvspaceClose(h);
        return rc;
    }
    kvspaceRef_t r;
    memset(&r, 0, sizeof r);
    if (kvspaceResolveRef(h, key, &r) != 0)
        return 1;
    kvspaceXvalueBodyView_t v;
    if (kvspaceXvalueBodyView(h, &r, key, &v) != 0)
        return 1;
    uint64_t a = rd64(v.head + 2), b = rd64(v.head + 10);
    printf("pow=%u flags=%u a=%llu b=%llu headlen=%u body_cap=%llu lt=%s body=%.*s\n",
           v.head[0], v.head[1], (unsigned long long)a, (unsigned long long)b,
           v.headlen, (unsigned long long)v.body_cap, (char *)(v.head + 18),
           (int)a, (char *)v.body);
    kvspaceClose(h);
    return 0;
}

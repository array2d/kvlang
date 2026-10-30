/* b1b：读取 /vthread/<vid>/‥pc 的 xvalue body view 并打印字段，供 shell 断言。 */
#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* shrink：把 ‥pc 重写为 langtype 基类型不符的槽（模拟 owned 后不合规），
 * 用于验证 runtime 回退仍能正确写 PC。 */
static int shrink(void *h, const char *key) {
    uint8_t *val = NULL;
    uint32_t vlen = 0;
    if (kvspaceGet(h, key, 0, &val, &vlen) != 0 || !val ||
        vlen < KVSPACE_XVALUE_HEAD_LEN)
        return 1;
    uint64_t a = 0;
    for (int i = 5; i >= 0; i--)
        a = (a << 8) | val[i];
    if (KVSPACE_XVALUE_HEAD_LEN + a > vlen)
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
    memcpy(saved, val + KVSPACE_XVALUE_HEAD_LEN, (size_t)a);
    if (kvspaceWriteNewPlace(h, key, KVSPACE_REF_INLINE,
                             KVSPACE_STORETYPE_SLACK, (uint32_t)a, 1024,
                             &body, err, sizeof err) != 0 ||
        !body)
        return 1;
    memcpy(body, saved, (size_t)a);
    /* ‥pc 是系统键（末段含 ‥）：契约不写 attr——但本测试要模拟
       「langtype 基类型不符」槽，特此自写 attr key 验证 runtime 回退。 */
    {
        uint8_t *lv = NULL;
        uint64_t lvn = 0;
        char ltk[1028];
        snprintf(ltk, sizeof ltk, "%s\xE2\x80\xA5" "langtype", key);
        if (kvspaceXvalueHeadNewSlack((const uint8_t *)lt,
                                      (uint64_t)strlen(lt),
                                      (uint64_t)strlen(lt), &lv, &lvn) != 0 ||
            lvn > 0xffffffffu)
            return 1;
        int r = kvspaceSetValue(h, ltk, lv, (uint32_t)lvn, err, sizeof err);
        free(lv);
        if (r != 0)
            return 1;
    }
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
    uint64_t len = 0;
    for (int i = 5; i >= 0; i--)
        len = (len << 8) | v.head[i];
    printf("storetype=%u ptr=%u len=%llu headlen=%u body_cap=%llu body=%.*s\n",
           v.head[6], v.head[7], (unsigned long long)len,
           v.headlen, (unsigned long long)v.body_cap,
           (int)len, (char *)v.body);
    kvspaceClose(h);
    return 0;
}

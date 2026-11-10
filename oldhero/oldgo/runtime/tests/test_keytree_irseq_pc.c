/* P1b4-a3：跳转目标 PC 的栈缓冲格式化。
 * 重点覆盖 frame_root 尾部 '/'：helper 与 jump_to 的 cursor prefix 必须先剥掉，
 * 否则顺序推进会出现 `//`。 */

#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                      \
    } while (0)

static void expect(const char *root, int irseq, const char *want) {
    char buf[KV_KEY_MAX];
    size_t n = kvlangKeytreeIrseqPcBuffer(root, irseq, buf, sizeof buf);
    CHECK(n == strlen(want));
    CHECK(strcmp(buf, want) == 0);
}

int main(void) {
    expect("/vthread/7/f", 3, "/vthread/7/f/[3,0]");
    expect("/vthread/7/f/", 3, "/vthread/7/f/[3,0]");
    expect("/vthread/7/f//", 3, "/vthread/7/f/[3,0]");
    expect("/", 1, "/[1,0]");
    expect("//", 1, "/[1,0]");
    expect("/a", 2147483647, "/a/[2147483647,0]");

    /* cap 不足：返回 0，不越界写。 */
    char small[8];
    CHECK(kvlangKeytreeIrseqPcBuffer("/abcdef", 12, small, sizeof small) == 0);
    /* 非法参数：返回 0。 */
    CHECK(kvlangKeytreeIrseqPcBuffer(NULL, 1, small, sizeof small) == 0);
    CHECK(kvlangKeytreeIrseqPcBuffer("/a", -1, small, sizeof small) == 0);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("ok: keytree irseq pc buffer\n");
    return 0;
}

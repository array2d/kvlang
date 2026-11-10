/* 语言常量运行期查询：供 Go/Python/Rust 扩展经 C ABI 取常量，避免各语言硬编码
 * 一份 kind 串。存储常量不在这里——它们在 kvspace 的 `kvspaceConstStorage`。
 *
 * 两层的查询入口必须分开：同一个函数返回两种层的东西，等于把「这个名字属于哪层」
 * 重新变成调用方要猜的问题。 */
#include <kvlang/const.h>

#include <stddef.h>
#include <string.h>

const char *kvlangConstLanguage(const char *name) {
    static const struct { const char *key; const char *val; } table[] = {
#define KVLANG_CONST_ENTRY(name) { #name, name },
        KVLANG_KV(KVLANG_CONST_ENTRY)
#undef KVLANG_CONST_ENTRY
    };
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].key, name) == 0)
            return table[i].val;
    return NULL;
}

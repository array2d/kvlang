#include "vmbin/directory_walk.h"

#include <errno.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>

#include "contract/key_resolution.h"
#include "contract/kvspace_interface.h"

/*
 * 内存来源：handler 层拿不到 `contract/allocator`（frame 里没有它），所以 allocator
 * 可传 NULL，那时用 libc。目录枚举是**冷路径**——每次 handler 调用一次，不是每条
 * 指令一次；同层的 `container.c` 读变长 body 时早已直接 malloc。
 */
static void *walk_libc_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size != 0U ? size : 1U);
}

static void walk_libc_deallocate(void *context, void *allocation, size_t size,
                                 size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

/* `key_directory` 只认有效分配器，所以这里把 NULL 具体化成 libc 那一对。 */
static runtimec_allocator_t walk_allocator_or_libc(
    const runtimec_allocator_t *allocator) {
    if (allocator != NULL && runtimec_allocator_is_valid(allocator)) {
        return *allocator;
    }
    runtimec_allocator_t libc;
    libc.context = NULL;
    libc.allocate = walk_libc_allocate;
    libc.deallocate = walk_libc_deallocate;
    return libc;
}

/*
 * source 的上下文：句柄 + **已解析**的目录路径。
 *
 * `key_directory` 把 source 与 context 一起持有到 close，所以它不能指向本函数的栈。
 * 解析结果（Ptr 跟随之后）也只在这里存在——把未解析的原始路径再传一遍会让跟随白做。
 */
typedef struct walk_source_context {
    void *handle;
    char directory[RUNTIMEC_DIRECTORY_WALK_KEY_CAPACITY];
} walk_source_context_t;

static runtimec_status_t walk_source(void *context, const char *directory,
                                     char *names, uint32_t capacity,
                                     uint32_t *output_length,
                                     uint32_t *output_count,
                                     uint64_t *output_generation) {
    const walk_source_context_t *source = (const walk_source_context_t *)context;
    (void)directory; /* 用 context 里解析好的那份，见上。 */
    if (source == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int status =
        kvspaceList(source->handle, source->directory, names, capacity,
                    output_length, output_count, output_generation, error_text,
                    (uint32_t)sizeof(error_text));
    if (status == 0) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /* 新接口唯一允许重试的失败，且它已写出所需长度与个数（storage.h 的契约）。 */
    if (status == -ENOSPC) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    return RUNTIMEC_STATUS_ERROR;
}

/*
 * 读一个 key 的值，供 `key_resolution` 判 Ptr。
 *
 * 这里仍用旧 `kvspaceGet`：读值属于**读写路径**，按裁决「durable 先不管」暂不切。
 * 封在这一点上，读写路径迁移时只改这个函数体，`directory_walk` 其余部分不动。
 */
static runtimec_status_t walk_reader(void *context, const char *key,
                                     const char **output_langtype,
                                     size_t *output_langtype_size,
                                     const char **output_body,
                                     size_t *output_body_size) {
    void *handle = context;
    if (output_langtype != NULL) {
        *output_langtype = NULL;
    }
    if (output_langtype_size != NULL) {
        *output_langtype_size = 0U;
    }
    if (output_body != NULL) {
        *output_body = NULL;
    }
    if (output_body_size != NULL) {
        *output_body_size = 0U;
    }
    if (handle == NULL || key == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /*
     * 新接口只给 offset，地址 = 基址 + offset。**不跟 Ptr**：跳数由
     * `key_resolution` 统一管，后端再跟一遍会让上限变成乘积且中途状态不可观察。
     *
     * 生命周期：这里返回的 body/langtype 是**借用**，只在「下一次对该 key 的
     * Realloc/Free 之前」有效（`kvspace.h` 头部条款）。`key_resolution` 在两次
     * 读之间不做写，所以这个借用跨度是安全的。
     */
    char error_text[128];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    const int resolve_status =
        kvspaceResolve(handle, key, 0U, &value_offset, error_text,
                       (uint32_t)sizeof(error_text));
    if (resolve_status != 0) {
        return RUNTIMEC_STATUS_INVALID; /* 键不存在：用当前路径 */
    }
    const uint8_t *base = kvspaceXvalueInProcessAddress(handle);
    if (base == NULL) {
        return RUNTIMEC_STATUS_ERROR; /* 非映射后端：本层不支持 */
    }
    const uint8_t *wire = base + value_offset;
    const uint64_t wire_size =
        kvspaceXvalueHeadTotalLen((const kvspaceXvalueHead *)(const void *)wire);
    if (wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return RUNTIMEC_STATUS_ERROR;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    if (!kvspaceXvalueHeadPaddingIsZero(head)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (output_body != NULL) {
        *output_body = (const char *)wire + KVSPACE_XVALUE_HEAD_LEN;
    }
    if (output_body_size != NULL) {
        *output_body_size = (size_t)kvspaceXvalueHeadLen(head);
    }
    const uint8_t *langtype = NULL;
    const uint16_t langtype_size =
        kvlangXvalueHeadLangtypeBounded(wire, (uint64_t)wire_size, &langtype);
    if (output_langtype != NULL && langtype != NULL) {
        *output_langtype = (const char *)langtype;
    }
    if (output_langtype_size != NULL) {
        *output_langtype_size = (size_t)langtype_size;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

bool runtimec_directory_walk_open(runtimec_directory_walk_t *walk,
                                  const runtimec_allocator_t *allocator,
                                  void *kvspace_handle, const char *directory,
                                  bool resolve_pointer, bool expand_extension) {
    /*
     * `walk == NULL` 唯一不能 memset 的情形，所以它单独判、先判。
     * 其余失败**必须**在 memset 之后返回：调用点拿到 false 后仍会调
     * `runtimec_directory_walk_close(&walk)`（那是唯一的清理出口），而 close 会
     * `free(source_context)` 并对 names/offsets 做 deallocate。若这里提前返回而
     * 没清零，那三处就是未初始化指针——`kvspace_handle == NULL` 是真实可达状态
     * （session 不接存储），不是防御性分支。
     */
    if (walk == NULL) {
        return false;
    }
    memset(walk, 0, sizeof(*walk));

    if (kvspace_handle == NULL || directory == NULL) {
        return false;
    }

    /* 新接口不展开 extindex。调用方还在用旧语义时明确失败——「悄悄少列几项」
     * 比「明确不支持」坏得多。 */
    if (expand_extension) {
        return false;
    }

    walk_source_context_t *source = malloc(sizeof(*source));
    if (source == NULL) {
        return false;
    }
    memset(source, 0, sizeof(*source));
    source->handle = kvspace_handle;

    if (resolve_pointer) {
        if (runtimec_key_resolve_prefix(walk_reader, kvspace_handle, directory,
                                        source->directory,
                                        sizeof(source->directory)) !=
            RUNTIMEC_STATUS_SUCCESS) {
            free(source);
            return false;
        }
    } else {
        const size_t size = strlen(directory);
        if (size + 1U > sizeof(source->directory)) {
            free(source);
            return false;
        }
        memcpy(source->directory, directory, size + 1U);
    }

    const runtimec_allocator_t resolved_allocator = walk_allocator_or_libc(allocator);
    runtimec_key_directory_reset(&walk->directory, &resolved_allocator, source,
                                 walk_source);
    walk->opened = true;
    if (runtimec_key_directory_refresh(&walk->directory, source->directory) !=
        RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_directory_walk_close(walk);
        return false;
    }
    return true;
}

bool runtimec_directory_walk_count(const runtimec_directory_walk_t *walk,
                                   int64_t *output_count) {
    if (walk == NULL || !walk->opened || output_count == NULL) {
        return false;
    }
    *output_count = (int64_t)walk->directory.count;
    return true;
}

bool runtimec_directory_walk_at(const runtimec_directory_walk_t *walk,
                                int64_t index, const char **output_name,
                                uint32_t *output_size) {
    if (walk == NULL || !walk->opened || index < 0) {
        return false;
    }
    return runtimec_key_directory_at(&walk->directory, (uint32_t)index,
                                     output_name, output_size) ==
           RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_directory_walk_close(runtimec_directory_walk_t *walk) {
    if (walk == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    void *source = walk->directory.source_context;
    const runtimec_status_t status =
        runtimec_key_directory_destroy(&walk->directory);
    /*
     * 先清指针再释放，且清的是 walk 自己持有的那份。
     *
     * `key_directory_destroy` 不会碰 `source_context`（它不拥有那个指针——所有权
     * 在本文件）。所以这里必须显式置 NULL：close 之后再 close（调用点就有「失败
     * 分支 close + 成功路径 close」两条），或 close 之后复用同一个结构体再次
     * open 失败，都会对同一个指针二次 free。`free(NULL)` 良定义，`free(已释放)`
     * 是 tcache 双释放。
     */
    walk->directory.source_context = NULL;
    free(source);
    walk->opened = false;
    return status;
}

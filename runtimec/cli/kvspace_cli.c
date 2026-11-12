/*
 * 组件：cli/kvspace_cli —— kvspace 的命令行探查工具。
 *
 * 类型相关的三个命令（newT / writeT / readT）只调 `kvlang_xvalue_store`，
 * **不含任何类型判断**：langtype 解析、文本↔字节转换都已在 kvlangxvalue 里，
 * 在这里再写一份就会出现「同一个值，两个工具读出来不一样」。
 *
 * 子命令分派用一张表，不用 if 链——新增命令只加一行，改分派逻辑的机会为零。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>

#include "kvlang/xvalue_store.h"

#define CLI_TEXT_CAPACITY 4096u
#define CLI_ERROR_CAPACITY 512u

/* 退出码：参数错误与「key 不存在」必须可区分——调用方脚本靠它分支。 */
enum cli_exit_code {
    CLI_EXIT_OK = 0,
    CLI_EXIT_FAILURE = 1,      /* 存储操作失败 */
    CLI_EXIT_USAGE = 2,        /* 参数错误 */
    CLI_EXIT_MISSING = 3       /* key 不存在（**不是一个错误**） */
};

typedef struct cli_context {
    kvlang_xvalue_store_t store;
} cli_context_t;

/* ── 存储命令 ──────────────────────────────────────────────────────── */

static int command_new_typed(cli_context_t *context, int argc, char **argv) {
    if (argc != 3) {
        return CLI_EXIT_USAGE;
    }
    kvlang_xvalue_store_write_new(context->store, argv[0], argv[1], argv[2]);
    return CLI_EXIT_OK;
}

static int command_write_typed(cli_context_t *context, int argc, char **argv) {
    if (argc != 2) {
        return CLI_EXIT_USAGE;
    }
    kvlang_xvalue_store_write_typed(context->store, argv[0], argv[1]);
    return CLI_EXIT_OK;
}

static int command_read_typed(cli_context_t *context, int argc, char **argv) {
    if (argc != 1) {
        return CLI_EXIT_USAGE;
    }
    char langtype[CLI_TEXT_CAPACITY];
    char value[CLI_TEXT_CAPACITY];
    if (!kvlang_xvalue_store_read(context->store, argv[0], langtype,
                                  sizeof langtype, value, sizeof value)) {
        /* 不存在：stdout 一个字都不打印，退出码独立。 */
        return CLI_EXIT_MISSING;
    }
    printf("%s\t%s\n", langtype, value);
    return CLI_EXIT_OK;
}

/* ── 探查命令（纯字节，不解释类型）─────────────────────────────────── */

/* 取一条 wire 的只读视图。不存在返回 false。 */
static bool peek_wire(kvlang_xvalue_store_t store, const char *key,
                      const uint8_t **out_wire, uint64_t *out_length) {
    uint64_t offset = 0;
    char error[CLI_ERROR_CAPACITY] = {0};
    const int status = kvspaceResolveOffset(store, key, KVSPACE_ANCHOR_ROOT, &offset,
                                            error, sizeof error);
    if (status == KVSPACE_MISSING) {
        return false;
    }
    if (status != KVSPACE_OK) {
        fprintf(stderr, "resolve failed: %s\n", error);
        exit(CLI_EXIT_FAILURE);
    }
    const uint8_t *base = kvspaceXvalueInProcessAddress(store);
    if (base == NULL) {
        fprintf(stderr, "存储没有进程内值区\n");
        exit(CLI_EXIT_FAILURE);
    }
    const uint8_t *wire = base + offset;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    *out_wire = wire;
    *out_length = kvspaceXvalueHeadTotalLen(head);
    return true;
}

static int command_head(cli_context_t *context, int argc, char **argv) {
    if (argc != 1) {
        return CLI_EXIT_USAGE;
    }
    const uint8_t *wire = NULL;
    uint64_t wire_length = 0;
    if (!peek_wire(context->store, argv[0], &wire, &wire_length)) {
        return CLI_EXIT_MISSING;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    printf("bodylen=%llu\tattrslen=%u\ttotal=%llu\tpadding=%s\n",
           (unsigned long long)kvspaceXvalueHeadLen(head),
           (unsigned)kvspaceXvalueHeadAttrsLen(head),
           (unsigned long long)wire_length,
           kvspaceXvalueHeadPaddingIsZero(head) ? "zero" : "NONZERO");
    return CLI_EXIT_OK;
}

static int command_attr(cli_context_t *context, int argc, char **argv) {
    if (argc != 1) {
        return CLI_EXIT_USAGE;
    }
    const uint8_t *wire = NULL;
    uint64_t wire_length = 0;
    if (!peek_wire(context->store, argv[0], &wire, &wire_length)) {
        return CLI_EXIT_MISSING;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    const uint8_t *cursor =
        wire + KVSPACE_XVALUE_HEAD_LEN + kvspaceXvalueHeadLen(head);
    const uint8_t *end = cursor + kvspaceXvalueHeadAttrsLen(head);
    if (cursor == end) {
        printf("(no attr records)\n");
        return CLI_EXIT_OK;
    }
    while (cursor + 3 <= end) {
        const uint8_t tag = cursor[0];
        const uint16_t length =
            (uint16_t)((uint16_t)cursor[1] | ((uint16_t)cursor[2] << 8));
        if (cursor + 3 + length > end) {
            fprintf(stderr, "attr 记录越界\n");
            return CLI_EXIT_FAILURE;
        }
        printf("tag=%u\tlen=%u\tdata=", (unsigned)tag, (unsigned)length);
        for (uint16_t index = 0; index < length; index++) {
            printf("%02x", cursor[3 + index]);
        }
        printf("\n");
        cursor += 3 + length;
    }
    return CLI_EXIT_OK;
}

static int command_list(cli_context_t *context, int argc, char **argv) {
    if (argc != 1) {
        return CLI_EXIT_USAGE;
    }
    int64_t count = 0;
    uint32_t max_name_length = 0;
    char error[CLI_ERROR_CAPACITY] = {0};
    int status = kvspaceListlen(context->store, argv[0], &count, &max_name_length,
                                error, sizeof error);
    if (status != KVSPACE_OK) {
        fprintf(stderr, "listlen failed: %s\n", error);
        return CLI_EXIT_FAILURE;
    }
    if (count == 0) {
        return CLI_EXIT_OK;
    }
    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    const uint32_t capacity = (uint32_t)count * (max_name_length + 1u);
    char *names = (char *)malloc(capacity);
    if (names == NULL) {
        fprintf(stderr, "分配缓冲失败\n");
        return CLI_EXIT_FAILURE;
    }
    status = kvspaceList(context->store, argv[0], names, capacity, &length, &listed,
                         &generation, error, sizeof error);
    if (status != KVSPACE_OK) {
        free(names);
        fprintf(stderr, "list failed: %s\n", error);
        return CLI_EXIT_FAILURE;
    }
    for (const char *name = names; name < names + length; name += strlen(name) + 1) {
        printf("%s\n", name);
    }
    free(names);
    return CLI_EXIT_OK;
}

static int command_delete(cli_context_t *context, int argc, char **argv) {
    if (argc != 1) {
        return CLI_EXIT_USAGE;
    }
    char error[CLI_ERROR_CAPACITY] = {0};
    const int status =
        kvspaceFree(context->store, argv[0], error, sizeof error);
    if (status == KVSPACE_MISSING) {
        return CLI_EXIT_MISSING;
    }
    if (status != KVSPACE_OK) {
        fprintf(stderr, "delete failed: %s\n", error);
        return CLI_EXIT_FAILURE;
    }
    return CLI_EXIT_OK;
}

static int command_copy(cli_context_t *context, int argc, char **argv) {
    if (argc != 2) {
        return CLI_EXIT_USAGE;
    }
    char error[CLI_ERROR_CAPACITY] = {0};
    const int status =
        kvspaceCp(context->store, argv[0], argv[1], error, sizeof error);
    if (status != KVSPACE_OK) {
        fprintf(stderr, "copy failed: %s\n", error);
        return CLI_EXIT_FAILURE;
    }
    return CLI_EXIT_OK;
}

/* ── 分派表 ────────────────────────────────────────────────────────── */

typedef struct cli_command {
    const char *name;
    int (*run)(cli_context_t *context, int argc, char **argv);
    const char *usage;
    const char *summary;
} cli_command_t;

static const cli_command_t COMMANDS[] = {
    {"newT", command_new_typed, "newT <key> <langtype> <value>",
     "按 langtype 构造 XValue 并写入"},
    {"writeT", command_write_typed, "writeT <key> <value>",
     "按目标已有 langtype 写入（类型从 attr 取，不猜）"},
    {"readT", command_read_typed, "readT <key>",
     "打印 <langtype>TAB<value>；key 不存在时退出码 3"},
    {"head", command_head, "head <key>", "打印原始 head 字段"},
    {"attr", command_attr, "attr <key>", "逐条打印 attr 记录（tag/len/data）"},
    {"list", command_list, "list <prefix>", "列直接子项（后端顺序）"},
    {"del", command_delete, "del <key>", "删除键"},
    {"cp", command_copy, "cp <source> <target>", "复制单键"},
};

static const size_t COMMAND_COUNT = sizeof COMMANDS / sizeof COMMANDS[0];

static void print_help(void) {
    printf("usage: kvspace-cli <command> [args]   (存储地址由 env KVSPACE 给出)\n\n");
    for (size_t index = 0; index < COMMAND_COUNT; index++) {
        printf("  %-32s %s\n", COMMANDS[index].usage, COMMANDS[index].summary);
    }
    printf("\nexit codes: 0 ok, 1 failure, 2 usage, 3 key not found\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return CLI_EXIT_USAGE;
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return CLI_EXIT_OK;
    }
    const char *dsn = getenv("KVSPACE");
    if (dsn == NULL || dsn[0] == '\0') {
        fprintf(stderr, "未设置 KVSPACE。本工具只接受 shm://，例如：\n"
                        "  KVSPACE=shm:///tmp/kvspace.shm kvspace-cli list /\n");
        return CLI_EXIT_USAGE;
    }
    cli_context_t context = {kvlang_xvalue_store_open(dsn)};
    if (context.store == NULL) {
        fprintf(stderr, "connect failed: %s\n"
                        "  只接受 shm://（例如 shm:///tmp/kvspace.shm）\n",
                dsn);
        return CLI_EXIT_USAGE;
    }
    int result = CLI_EXIT_USAGE;
    for (size_t index = 0; index < COMMAND_COUNT; index++) {
        if (strcmp(argv[1], COMMANDS[index].name) == 0) {
            result = COMMANDS[index].run(&context, argc - 2, argv + 2);
            break;
        }
    }
    if (result == CLI_EXIT_USAGE) {
        print_help();
    }
    kvlang_xvalue_store_close(context.store);
    return result;
}

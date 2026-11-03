#ifndef VMBIN_CORE_OPERATIONS_H
#define VMBIN_CORE_OPERATIONS_H

/*
 * 组件：vmbin/core_operations
 * 作用：vmbin 自己实现语义、不交 registry / route 的那几条核心规则的 opid。
 * 联系：interpreter 按这些 opid 分派；测试用 vmkv 的能力表逐条核对，保证这里
 *       的数字不会和 specification/rwir_catalog.def 漂开。
 * 约束：本文件不复制能力行，只复制 opid 数值；数值的唯一权威仍是 catalog，
 *       所以必须有测试对表，不允许只靠人工同步。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_OPERATION_IDENTIFIER_COPY UINT32_C(0x0001)
#define VMBIN_OPERATION_IDENTIFIER_CALL UINT32_C(0x0024)
#define VMBIN_OPERATION_IDENTIFIER_RETURN UINT32_C(0x0025)
#define VMBIN_OPERATION_IDENTIFIER_GOTO UINT32_C(0x0026)
#define VMBIN_OPERATION_IDENTIFIER_BRANCH UINT32_C(0x0027)

#ifdef __cplusplus
}
#endif

#endif

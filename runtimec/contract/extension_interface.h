#ifndef RUNTIMEC_CONTRACT_EXTENSION_INTERFACE_H
#define RUNTIMEC_CONTRACT_EXTENSION_INTERFACE_H

/*
 * 组件：contract/extension_interface
 * 作用：TASK-047 的扩展 handler 值访问接口的**声明**。
 *
 * 为什么声明与实现分居两层：类型（`kvlang_runtime_error_t` 等）住在公共头
 * `kvlang/runtimec.h`，按分层表只有 `contract` 能 include 它；而实现要吃
 * `binrwir` 的 frame/instruction 与 `vmbin` 的 value_access，按分层表只有
 * `engine` 及以上能 include 它们。两边都要，所以声明进 contract、实现进 engine。
 *
 * 这不是「为了绕分层而拆」：声明与实现本来就有两个消费者（公共头对外承诺、
 * engine 对内实现），放在各自能看见依赖的那一层是唯一不撒谎的位置。
 */

#include <kvlang/runtimec.h>

#endif

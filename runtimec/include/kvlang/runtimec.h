#ifndef KVLANG_RUNTIMEC_H
#define KVLANG_RUNTIMEC_H

/*
 * 组件：kvlang/runtimec —— runtimec 对外的**全部** ABI。
 *
 * 只有三个名字：
 *   - `kvlang_runtime_kvspace_handler_t`：kvspace 句柄，`kvspaceConnect` 的原样返回值；
 *   - `kvlang_runtime_create`：建一个运行时；
 *   - `kvlang_runtime_execute`：跑一个 vthread。
 *
 * 没有错误结构、没有状态枚举、没有退出码：失败写进 `/vthread/{vid}/‥status`，
 * 调用方从存储里读。也没有 destroy——运行时跟随进程生命周期。
 */

#if defined(_WIN32)
#  if defined(KVLANG_RUNTIMEC_BUILDING_LIBRARY)
#    define KVLANG_RUNTIMEC_EXPORT __declspec(dllexport)
#  else
#    define KVLANG_RUNTIMEC_EXPORT __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define KVLANG_RUNTIMEC_EXPORT __attribute__((visibility("default")))
#else
#  define KVLANG_RUNTIMEC_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* kvspace 句柄：运行时只透传，不解释。 */
typedef void *kvlang_runtime_kvspace_handler_t;

/* 运行时句柄：不透明。 */
typedef struct kvlang_runtime kvlang_runtime_t;

/* 建一个运行时。失败即终止（p0：没有第二个降级点）。 */
KVLANG_RUNTIMEC_EXPORT kvlang_runtime_t *kvlang_runtime_create(
    kvlang_runtime_kvspace_handler_t kvspace_handler);

/*
 * 执行一个 vthread，直到它结束或撞上不属于本 runtime 的 rwir。
 *
 * **唯一参数是 `vthread_identifier`**：位置、帧、函数全部由它推出来
 * （`/vthread/<vid>/‥pc` → 帧 → `‥lib`）。结局看 `‥status`，断点看 `‥pc`。
 */
KVLANG_RUNTIMEC_EXPORT void kvlang_runtime_execute(
    kvlang_runtime_t *runtime, const char *vthread_identifier);

#ifdef __cplusplus
}
#endif

#endif

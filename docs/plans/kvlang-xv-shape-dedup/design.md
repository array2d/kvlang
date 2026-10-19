# #377 + #380：形状解析收敛 与 死代码清理

两个 issue 改同一片区域（`xv_head_t` / `xvalue.c` / `rwir_xvalue.c` / `rwir_ndarray.c`），一并做。

## 一、#377 现状（实测的 Parse 次数）

`xv_head_t` 只透出 langtype 串，形状靠下游各自 `kvlangLangtypeParse`。同一份 langtype 在一处被反复解：

| 调用点 | 现有 Parse 次数 |
|---|---|
| `rwir_ndarray.c:51-67` `ndarray·dim` | **2**（`guard` :8 + 自己 :59） |
| `rwir_ndarray.c:69-97` `ndarray·shape` | **2**（`guard` :8 + 自己 :78） |
| `rwir_ndarray.c:19-49` `ndarray·numel` | 1（:24） |
| `rwir_xvalue.c:85-151` `xv.at` | 1（:103，外加 `xv_elem_size` 的 memcpy+查表） |
| `rwir_xvalue.c:155-249` `xv.set` 快路径 | 1（:180） |
| `rwir_xvalue.c:213-249` `xv.set` 慢路径 | **3**（:217 `Kind` + :218 `ElemSize` + :220 `Parse`） |
| `rwir_xvalue.c:251-306` `xv.reshape` | **3**（:261 `Kind` + :262 `ElemSize` + :268 `Parse`） |

`ndarray·dim`/`shape` 的第 2 次是纯冗余（guard 里已解过一次）。

## 二、#377 方案

**不新造类型**——`kvlangLangtype`（`runtime_internal.h:94-100`）已有 `kind/kind_len/ndim/dims[8]/array_len`，只缺 `elem_size`。

### 2.1 `xv_head_t` 增字段 + 惰性填充

```c
typedef struct {
    kvspaceHead_t head;      /* 变量路径的数据源；#380 一并删掉无人读的 is_ptr */
    const uint8_t *langtype;
    int32_t langtype_len;
    int32_t body_offset;
    int32_t body_len;
    /* 按需解出的形状；只读 langtype/body_offset 的调用点不付这次 Parse */
    kvlangLangtype kx;
    int32_t elem_size;
    bool kx_done;
} xv_head_t;

static inline const kvlangLangtype *xv_head_kx(xv_head_t *o) {
    if (!o->kx_done) {
        kvlangLangtypeParse(o->langtype, o->langtype_len, &o->kx);
        o->elem_size = kvlangElemSizeOf(o->kx.kind, o->kx.kind_len);
        o->kx_done = true;
    }
    return &o->kx;
}
```

采**惰性**而非 eager：`xv.langtype`(`:340-353`)、`xv.bodylen`(`:355-`) 只读串与长度，不该白付一次 Parse。

### 2.2 新增 `kvlangElemSizeOf(kind, kind_len)`

现有 `kvlangXvalueElemSize(const char *kind)`（`xvalue.c:278`）要求 NUL 终止串，调用方得先 `memcpy` 到栈缓冲。加一个吃 `(ptr,len)` 的版本免掉这次拷贝，原函数改为调它。

放 `xvalue.c`（langtype 解析的归属层），声明进 `runtime_internal.h`。

### 2.3 下游改造

`ndarray_shape_guard` 改收非 const `xv_head_t *`，内部用 `xv_head_kx`；`dim`/`shape`/`numel` 直取 `kx->ndim` / `kx->dims` / `kx->array_len`。

`xv.at` / `xv.set` 快路径改用 `xv_head_kx(&h)`。

`xv.set` 慢路径、`xv.reshape` 处理的是 `kvlangXvalue_t`（非 head），就地收敛为「一次 `kvlangLangtypeParse` + `kvlangElemSizeOf`」，替掉现在的 Kind+ElemSize+Parse 三连。

**不动**：`xv.reinterpret`(`:318-320`) 解析的是**入参字符串**，不是 xvalue 的 head，与本方案无关。

## 三、#380 死代码（均零调用，已 grep 全仓）

| 位置 | 删除内容 |
|---|---|
| `xvalue.c:129-159` + `runtime_internal.h:226` | `kvlangXvalueHead` + `xh_fill_head` —— 328B head 填充路径，#360 残留 |
| `rwir_func.c:112-137` + `runtime_internal.h:585` | `kvlangBuiltinTryParseNumber` —— `oldhero/.../coerce.go` 移植残留 |
| `rwir_string.c:41-46` | `new_char32_cp` |
| `rwir_map.c:124-130`（及 `:145` 的 free） | `names[]` 纯死分配 |
| `rwir_internal.h:12` | `xv_head_t.is_ptr`（两个 inline 构造里的赋值一并删） |
| `rwir_array.c:368, 377` | `n >= 2 ? in[1] : in[0]` 恒真三元 |
| `rwir_internal.h:4-5` | 注释「不建 328B head」与实际内嵌 `kvspaceHead_t` 矛盾，改写措辞 |

**订正**：审查报告称 `runtime_internal.h:11-25` 的 `kvspaceHead_t` 副本「多一个 body_cap」——复验后**不成立**，与 ABI 头 `kvspace.h:40-53` 字段逐一相同。此处只改注释，不动结构。

## 四、跨仓判定

只动 `runtime/src/` 内部：无 wire / kind / storetype / ABI 符号变化。**非跨仓**，kvlang 单仓即可，无需同步 kvspace 三仓。

## 五、验证

1. `tutorial/test.py` 三后端 222/0
2. `benchmark/run.py` 与 `results-v0.3.3-*.csv` 逐点对比，确认不劣化
3. 专项：`xv.at`/`xv.set`/`xv.reshape`/`ndarray·dim|shape|numel` 各一条（含多维 tensor 与 `[n]char/utf8`），断言形状结果不变
4. 编译期：`-Wall -Wextra` 无新告警（删字段后确认无遗漏引用）

## 六、风险

- `xv_head_t` 从 ~360B 增至 ~420B（栈）。可接受；如后续要压，再议把 328B `head` 移出栈。
- `kvspaceDecodeHead` 只在 **FIXED_LARGE / `[`开头的 SLACK** 填 `ndim`/`dims`——但 `xv_head_kx` 不依赖 `head.ndim`，一律从 langtype 串自解，故不受该条件影响。

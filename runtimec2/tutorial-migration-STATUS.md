# runtimec2 array 双形态迁移 — 交接状态（2026-11 编码会话中断）

## 背景与设计裁决（用户已敲定，勿再改方向）

array 两种物理形态彻底分离为两个 lib，**创建新 key/XValue 的 rwir 必须带 `new`/`fill`/`copy`**，无隐式跨形态转换：

- **compactarray**（compact 形态，单一 XValue）：`new`/`fill`/`copy`/`newslice`/`newreshape`/`newreinterpret`/`at`/`set`/`numel`/`dim`/`shape`/`langtype`/`bodylen`/`parselangtype`
- **scatterarray**（stringkeymap 散列形态）：`new`/`newscatter`/`newcompact`/`len`/`at`/`set`/`append`/`slice`
- 铁律：`compactarray·set` 仅同槽 patch（异槽必须先 `compactarray·copy`）；`scatterarray·append` 只收散列容器（compact 源 TypeError，须先显式 `scatterarray·newscatter`）
- 旧名 `array·*`/`xv·*`/`ndarray·*` 全部废弃

## 已完成

### spec（已全量改）
- `stdlib/kvlang/spec/03-类型系统/09-数组形态.kv`（两套算子族表 + 边界铁律）
- `10-compact形态`、`11-stringkeymap形态`、`12-稠密vs稀疏`、`13-ndarray元数据算子`（拆 compactarray·numel/dim/shape + scatterarray·len）
- `02-kvspace模型/07-数组两种物理形态`、`04-layout语义/06-控制流脱糖`、`05-runtime语义/03-成员访问`、`05-runtimec内建`、`附录/07-内建函数`

### ISA
- `layout/tests/fixtures/isa_native.txt`：换名 + 新增 6 行（count 299→305，hash 已重算）
- `runtime/src/isa_sig.c`：注释换名（位置表未加新行——**static_assert 300 行断言需同步**）
- `runtimec2/src/rwir_gen.cpp`：已用 `tools/gen_ops.py` 重新生成（新名 Native）

### layout（Rust）
- `layout/src/{ast,code,lower,parser}.rs`：全部旧名换新名（批量替换，已 cargo build 通过）

### runtimec2（vmkv.cpp）
- native 分派 fn== 链全部换新名
- `compactarray·set` 异槽 → TypeError（已删拷贝创建分支）
- `scatterarray·append` compact 源 → TypeError（已删隐式 ensure_scattered）
- 新增 `compactarray·copy` 分派
- **2026-11 补全**：`scatterarray·new`/`len`/`at`/`set` 分派已实现，`compactarray·newslice`、`scatterarray·slice`（散列成员重排）已实现

### stdlib
- `stdlib/xv.kv`：swap 改 `copy`+同槽 set；xv·at/reshape/numel 换新名
- `stdlib/string.kv`、`stdlib/kv.kv`：`scatterarray·append`（散列容器，语义对）
- `stdlib/kvlang/kvlangbrief.kv`：注释更新

### tutorial（已批量换名 55+ 文件）
- 机械改名：xv·at→compactarray·at、xv·reshape→compactarray·newreshape、xv·reinterpret→compactarray·newreinterpret、xv·langtype/bodylen/parselangtype→compactarray·*、ndarray·numel/dim/shape→compactarray·*、array·fill→compactarray·fill
- array·scatter→scatterarray·newscatter、array·compact→scatterarray·newcompact
- array·append→scatterarray·append（全仓库批量）

## 未完成 / 需注意

1. ~~tutorial/04-ndarray/array_append.kv 回滚~~ **已修**（显式 newscatter + scatterarray·len）
2. ~~array_slice.kv~~ **已修**（compactarray·newslice 分派补上）
3. ~~array_slice_hole.kv~~ **已修**（scatterarray·slice 散列成员重排实现）
4. ~~xv_shape.kv~~ 已改 compactarray·set（同槽，✓）
5. ~~kvlangbrief.kv / subscript_write.kv 注释~~ 已更新
6. **C runtime 未改**（`runtime/src/{myrwircaps,rwir_array,rwir_xvalue,rwir_ndarray}.c` 仍用旧名）——layout 生成新名 rwir 后 C runtime 不认会 Handoff，**对拍基准需同步**
7. ~~runtimec2 scatterarray 新分派~~ **已实现**（new/len/at/set，另有 newcompact/newscatter/append/slice/compactarray·newslice 全链路）
8. ~~isa_sig.c static_assert 300 行~~ **已改 305**（新增 compactarray·new/copy + scatterarray·new/len/at/set 六行）
9. **测试已跑通 shm**：`make all` 重建 + stage_embed 刷新 + 前端重链后，tutorial shm 全量 **PASS 159 / FAIL 73**，04-ndarray 全绿
10. ~~spec 里 kvlangbrief.kv 的 kvspace·listlen~~ 已改 scatterarray·len（array_append.kv 期望）

## 本批次修复（2026-11 编码会话）
- **array_append.kv**：显式 newscatter + scatterarray·len
- **isa_sig.c**：+6 行（compactarray·new/copy、scatterarray·new/len/at/set）、static_assert 305
- **layout/lower.rs**：scatterarray·len 返回 int64、scatterarray·at 返回容器元素类型
- **runtimec2/vmkv.cpp**：
  - scatterarray·new/len/at/set 四分派（marker+成员目录、ListLen、`base·[i]` 读写）
  - compactarray·newslice 分派（compact 裁剪，错误名对齐）
  - scatterarray·slice 散列成员重排（带洞按 memindex 元素序列 + 删旧坐标）
  - **append 读参 bug**：`read_elem_wire(1)` 用裸索引当 reg 索引 → 改 `ins.rd[1]`（曾致 append 写入数组字面量第 2 元素）
  - **newscatter 清旧成员**：kvspaceDelTree 对 `·` 前缀不删（shm）→ 逐成员 ListLen+ListAt+Del
  - **fill 空数组**：total=0 时 raw 传 0 长度（原 1 字节占位致 encode 校验失败）
  - **kvspace·listat 写前 ensure_reg_key**（wr key 未解析致 write_text 失败）
- **运行时后端差异**：`kvspaceXvalueBodyView` 仅 shm 可用（redis 返回失败致 reg_body 取不到 head → newscatter/copy/langtype 挂）。**tutorial 只跑 shm**（与用户裁决一致）

## 剩余失败（非本次迁移，shm 基线缺口）
- `10-types/06-unconstrained-param`：空 kindexp 读参 `kvspace·listlen` 返 0（期望 3）——指针/空约束路径
- `10-types/09-deref`：`*p` 解引用读到空——指针解引用路径
- `11-string/{03-slice-find,04-cmp-concat,08-strconv}`：`string·find/cmp/formatint` c2 未实现
- `15-vthread/*`：`vthread·create/run/sleep/call` c2 未实现（byteseek 方向）
- `14-networld/*`：engine.rs 断言（kvspace se…）非 c2 层

## 性能护栏（#499）
- 新增分派走 fn== 链，不碰 fast kernel / Typed 快路径
- 每批改动后 shm 10-case A/B（隔离 $ORIGIN、repeat=3、热 TU >2% 停）

## 下一步建议顺序
1. **C runtime 换名**（myrwircaps/rwir_array/rwir_xvalue/rwir_ndarray）——对拍基准同步（唯一遗留迁移项）
2. 指针/空 kindexp 两例（10-types）——非迁移，独立 issue
3. string/vthread c2 缺口——按 byteseek 计划跟进
4. `make all` + stage_embed 重建发布态二进制（本会话已跑，交付前重跑一遍）

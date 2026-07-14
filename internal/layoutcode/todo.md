# layoutcode todo

> 最高标准设计见 `internal/keytree/最高标准设计.md` 八、完整路径示例。
> 本文档记录当前实现与标准之间的偏差。

---

## P4：HandleCall Copy → Link

**状态**：待完成  
**影响**：执行语义变更，需完整集成测试

### 问题

当前 `HandleCall` 将 `/func/<pkg>/<name>/[i,j]` **逐槽复制**到
`/vthread/<vtid>/<frame>/[i,j]`，并在复制时做参数替换（形参 → 实参路径）。

缺陷：
- 每个活跃帧都有一份代码副本，浪费空间
- 返回时需逐槽枚举删除，而不是一次 `DelR`
- "帧 = 代码链接 + 局部参数槽"的纯洁性丢失
- `copyFunc` 是一个复杂的递归函数，难以维护

### 标准模型（Link-based）

```
call add(x=3, y=4) -> z     在 frame=/vthread/1/calc 中执行

1. kv.Link /func/add  →  /vthread/1/calc/add   挂载代码（零拷贝）
2. kv.Set  /vthread/1/calc/add/a  =  3         绑定实参
3. kv.Set  /vthread/1/calc/add/b  =  4
   PC = [0,0]，进入 /vthread/1/calc/add 帧执行

add 执行完毕，return：
4. kv.Get  /vthread/1/calc/add/c  →  读取返回值
5. kv.Set  /vthread/1/calc/z  =  7             写回调用方槽
6. kv.DelR /vthread/1/calc/add                 一次销毁帧（Link + 所有局部槽）
   PC 回到 /vthread/1/calc 的下一条指令
```

不变式：路径深度 = 调用栈深度，无需额外栈结构。

### 涉及文件

| 文件 | 当前 | 标准 |
|------|------|------|
| `layoutcode.go` `HandleCall` | `copyFunc` 逐槽复制 + 参数替换 | `kv.Link(funcPath, framePath)` + `kv.Set` 绑参 |
| `layoutcode.go` `HandleReturn` | `kv.List` 枚举子项逐一 `kv.Del` | `kv.DelR(framePath)` |
| `layoutcode.go` `copyFunc` | 递归复制函数 | **整体删除** |
| `kvcpu/controlflow.go` `resolveLabel` | 基于复制后的槽查找 | 逻辑不变，路径语义自然对齐 |

### 前置条件

- `kvspace.KVSpace` 接口需实现 `Link(src, dst string) error`
- `kvspace.KVSpace` 接口需实现 `DelR(prefix string) error`（已有则确认语义）

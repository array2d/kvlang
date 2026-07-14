# layoutcode todo

> 对齐目标：`internal/layoutcode/最高标准设计.md`
> 本文档记录当前实现与标准之间的偏差。

---

## ✅ P5：PC 改为绝对路径

**完成**：PC 全面改为绝对路径，`Decode(kv, pc)` 无需 vtid 参数，
`HandleCall` / `HandleReturn` 基于绝对 PC 操作帧路径。

---

## P4：HandleCall Copy → Link

**状态**：待完成（设计见 `最高标准设计.md §三.2`）

**问题**：当前 `copyFunc` 逐槽复制 `/func/<pkg>/<name>/[i,j]` 到帧路径，并做参数值替换。
缺陷：帧有独立代码副本、`copyFunc` 递归复杂、无法与 Link-based 帧语义对齐。

**标准**：
```
kv.Link(/func/<pkg>/<name>, pc)        零拷贝挂载
kv.Set(pc+"/"+param, actualVal)        绑实参（仅局部槽，无代码副本）
kv.DelR(pc)                            return 时一次销毁 Link + 局部槽
```

**前置条件**：
- `kvspace.KVSpace.Link` 接口已实现（`Link(target, linkpath string) error`）
- `kvspace.KVSpace.Unlink` 接口已实现（`DelR` 含链接时只删链接本身）

**涉及文件**：
| 文件 | 变更 |
|------|------|
| `layoutcode.go HandleCall` | 替换 `copyFunc` 为 Link + Set 绑参；~55 行 → ~20 行 |
| `layoutcode.go copyFunc` | **整体删除** |
| `layoutcode.go HandleReturn` | 已用 `kv.DelR`，无需改动 |

# lower todo

> 对齐目标：`internal/lower/最高标准设计.md`

---

## ✅ P10：确保 if → br 全覆盖，使 kvcpu 可删除 OpIf 兼容 case

**现状**：`lowerIf` 已将 `*ast.IfStmt` 展开为 `BlockStmt + br`，但
`kvcpu/controlflow.go` 中仍保留 `case op.OpIf` 兼容分支（转发给 `brToCall`）。

**标准**：lower 保证写入 `/func/` 前不存在 `if` opcode；
`kvcpu/controlflow.go` 的 `OpIf` case 即可安全删除。

**验收**：增加 lower 输出断言（或测试），确认无 `Instruction.Opcode == "if"`；
同步删除 `kvcpu/controlflow.go` 中的 `case op.OpIf`。

---

## P11：for / break / continue lowering

**现状**：`lower.go` 明确注释"for 循环（路径迭代）暂不 lowering，
待执行层迭代原语就绪后再处理"；`*ForStmt`、`*BreakStmt`、`*ContinueStmt` 原样透传。

**前置条件**：kvcpu / layoutcode 确定迭代原语语义（`iter` / `next` opcode 或 `br` 循环展开）。

**标准**：
- `for path { body }` → `while`-style `BlockStmt` 循环（`_for_cond` + `_for_body` + `_for_exit`）
- `break` → `call _for_exit_N`
- `continue` → `call _for_cond_N`

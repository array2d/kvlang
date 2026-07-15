# kvlang GitHub 项目优化评估

> 按 7 维度评估，已完成标记 ✅，剩余待办按优先级排列。

---

## 1. 文档/规范（3→5/10）

- [x] LICENSE (MIT)
- [x] 英文 README + mermaid 架构图 + CI badge
- [x] 5 分钟入门教程（tutorial/ 6 步）
- [ ] **LANGUAGE_SPEC.md**：从 grammar.bnf 扩展语义说明
- [ ] **API 参考**：builtin 函数一览表（参数、返回值、示例）

## 2. 测试覆盖 & CI（2→5/10）

- [x] GitHub Actions CI（build/vet/test，linux + macos）
- [x] CI badge
- [x] 多平台 Release CI（tag 触发，linux/darwin amd64/arm64）
- [ ] **补核心包单元测试**：kvcpu、lower、vthread、parser
- [ ] **make cover** 覆盖率报告

## 3. 可复现构建（6→7/10）

- [x] tag v0.1.0 + CHANGELOG
- [x] 多平台交叉编译（CI release job）
- [ ] **go install 验证**：确认 go install ... @latest 可用

## 4. 依赖复杂度（保持 7/10）

- [x] README 显式标注"仅 2 运行时依赖"
- 无需改动。极简依赖是卖点。

## 5. 示例/入门（4→6/10）

- [x] tutorial/ 6 步渐进教程
- [ ] **demo gif/asciicast**：README 中嵌入终端录制
- [ ] **在线 playground**（GitHub Pages + WASM？）

## 6. 安全/沙箱友好（?→4/10）

- [ ] **SECURITY.md**：文档化沙箱模型（KV 路径隔离、无 FS/网络直访）
- [ ] **资源限制文档**：最大 vthread 数、递归深度

## 7. 维护活跃度（3→7/10）

- [x] tag v0.1.0 + CHANGELOG
- [x] CONTRIBUTING.md
- [x] Issue 模板（bug report + feature request）
- [x] ROADMAP.md
- [ ] **定期 release**：每 2-4 周打 tag

---

## 剩余待办（按优先级）

| # | 项目 | 耗时 | 影响 |
|---|------|------|------|
| 1 | LANGUAGE_SPEC.md 语言规范 | 2 h | 4/5 |
| 2 | 补核心包单元测试 | 4 h | 3/5 |
| 3 | make cover 覆盖率报告 | 15 min | 3/5 |
| 4 | demo gif / asciicast | 1 h | 3/5 |
| 5 | SECURITY.md 安全模型 | 30 min | 2/5 |
| 6 | API 参考（builtin 一览表） | 1 h | 2/5 |
| 7 | go install 验证 | 5 min | 2/5 |

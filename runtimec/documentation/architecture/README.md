# runtimec architecture documents

本目录只描述 runtimec 的唯一 BinRWIR Compiler 主干。`runtimec/README.md` 是总纲，
本目录把静态 key 到 XValue 基址、vmkv 唯一执行 `argaddr = xvalue_address + 8`、组件
所有权、`opid + argidx + argaddr` 编码、编译生命周期、每条 logical
instruction 的强制完成边界、完成提交/未完成丢弃和 Read/Write 地址别名展开为可审查文档。

JIT 仅是 `binrwir_compiler/jit/` 的内部机器码实现，不是独立 VM 或并列顶层组件。

- `runtimec-architecture.md`：组件所有权、compilation 生命周期、状态与依赖规则。
- `capability-map.md`：每项 capability 的唯一所有者与禁止职责。
- `runtimec-trunk.mmd`：从明文 RWIR、XValue 基址解析、固定 `+8` 派生到完成提交或未完成丢弃的完整主干。
- `runtimec-execution.mmd`：XValue 基址验证、VMKV 地址派生、编译、执行与 JIT completion/discard 时序。
- `runtimec-dependencies.mmd`：允许依赖和明确禁止的反向依赖。
- `rwir-binrwir-aliasing.mmd`：同地址 Read/Write 别名在 completed commit 与 incomplete discard 中的行为。

四张 `.mmd` 是图形事实源；同名 `.png` 与 `.svg` 必须由当前图源同步生成，禁止保留与
图源不一致的旧渲染物。

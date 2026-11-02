# runtimec architecture documents

本目录只描述 runtimec 的唯一 Compiler 主干。`runtimec/README.md` 是总纲，本目录把组件
所有权、编译生命周期、执行顺序和 Read/Write 别名展开为可审查文档。

- `runtimec-architecture.md`：组件所有权、compilation 生命周期、状态与依赖规则。
- `capability-map.md`：每项 capability 的唯一所有者与禁止职责。
- `runtimec-trunk.mmd`：从 specification 到确定执行入口的完整主干。
- `runtimec-execution.mmd`：repository 命中与未命中时的编译、选择、执行时序。
- `runtimec-dependencies.mmd`：允许依赖和明确禁止的反向依赖。
- `rwir-binrwir-aliasing.mmd`：myrwircap 映射、canonical operation 与 Read/Write 别名。

四张 `.mmd` 是本轮更新的图源。已有 `.png` 与 `.svg` 只是此前生成物，本轮不渲染、
不更新；架构审查必须以 Mermaid 源为准。

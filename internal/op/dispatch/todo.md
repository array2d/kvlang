# op/dispatch 设计问题

## P1-4 `IsRelative`/`isNumber` 与 `internal/op/builtin` 重复定义
**文件**：`dispatch.go`  
`dispatch.IsRelative` ≡ `builtin.isRelative`，`dispatch.isNumber` ≡ `builtin.isImmediateNumber`。
应提取到公共包（如 `internal/op/param`），两处统一引用。

## P1-10 `OpTask`/`ParamRef` 张量专用类型混入通用分发包
**文件**：`dispatch.go`  
`OpTask`, `ParamRef` 是张量计算专有的 wire format，定义在通用 `dispatch` 包中。
非 tensor 的 VType 不需要这套类型，但被强制引入同一个包。
应将 `OpTask`/`ParamRef` 移到 `internal/op/dispatch/tensor` 子包，
`dispatch` 包仅保留通用路由逻辑（`Select`/`ListBackends`/`BackendSupports`）。

# op/builtin 设计问题

## P0-2 `isImmediateNumber` / `isImmediateBool` 应导出供 `kvcpu` 复用
**文件**：`resolve.go`  
`internal/kvcpu/execute.go:isCopyOp` 内嵌了相同的数字字面量判断逻辑。
将 `isImmediateNumber` → `IsImmediateNumber`，`isImmediateBool` → `IsImmediateBool` 导出，
`isCopyOp` 直接复用，消除重复。

## P0-6 三张 map 描述同一套内置算子，新增算子需同步三处
**文件**：`ops.go` + `builtin.go`  
1. `nativeOps map[string]bool` — 是否原生算子
2. `nativeSigs map[string]string` — 算子签名文本
3. `registry map[string]Op` — 算子实现（`builtin.go`）

新增一个算子必须同步修改三处，缺一不可且无编译检查。
应将三者合并为单一的自描述结构：
```go
type opDef struct {
    sig  string
    impl Op
}
var ops = map[string]opDef{ ... }
```

## P1-4 `isRelative`/`isNumber` 与 `internal/op/dispatch` 重复定义
**文件**：`resolve.go`  
`dispatch.IsRelative` ≡ `builtin.isRelative`，`dispatch.isNumber` ≡ `builtin.isImmediateNumber`。
应提取到公共包（如 `internal/op/param`），两处统一引用。

## P1-12 `str.set` 名称误导，语义是类型擦除而非 str 命名空间操作
**文件**：`ops.go`  
`str.set` 将任意类型值 `display()` 后存为 `kvspace.Str`。
名称暗示"str 命名空间的赋值"，实际是"转换为字符串"。
现有类型化字面量赋值（`0 -> ./x`）已覆盖原用途，`str.set` 应重命名为 `tostr`，
或移入 `strVType` 作为 `str.from` 实现，与 vtype 体系对齐。

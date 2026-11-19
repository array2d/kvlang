# architect

设计者：miaobyte
规则制定级别：最高
设计日期：2026/11/12

## 1. kvspace 依赖组件

- blockmalloc — https://github.com/array2d/blockmalloc
- slotsboxmalloc — https://github.com/array2d/slotsboxmalloc

## 2. kvlang/kvlangxvalue 依赖

- kvspace — https://github.com/array2d/kvspace

## 3. kvlang/layout 依赖

- kvspace — https://github.com/array2d/kvspace
- kvlangxvalue — https://github.com/array2d/kvlang（`kvlang/kvlangxvalue/`）

## 4. kvlang/runtimec 依赖

- kvspace — https://github.com/array2d/kvspace
- kvlangxvalue — https://github.com/array2d/kvlang（`kvlang/kvlangxvalue/`）
- v9 — https://github.com/array2d/kvlang（`kvlang/v9/`）

## 5. byteseek 依赖

- kvlangxvalue — https://github.com/array2d/kvlang（`kvlang/kvlangxvalue/`）
- kvlang/runtimec — https://github.com/array2d/kvlang（`kvlang/runtimec/`）
- kvlang/layout — https://github.com/array2d/kvlang（`kvlang/layout/`）

## 6. v9 依赖

- kvspace — https://github.com/array2d/kvspace
- kvlangxvalue — https://github.com/array2d/kvlang（`kvlang/kvlangxvalue/`）

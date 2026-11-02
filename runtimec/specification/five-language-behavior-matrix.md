# 五语言行为决策矩阵

<!-- runtimec-behavior-matrix-version: 2 -->

本文件是 runtimec 语言行为决策的唯一事实源，不是实现进度表。任何行为进入
`specification/`、`vmkv/`、`vmbin/` 或 `binrwir_compiler/jit/` 前，必须先在这里形成完整
决策。五种语言一致时，kvlang 遵从共同直觉；五种语言分裂时，必须明确选择
阵营、边界样例和非法输入结果。

`tools/architecture/check_behavior_matrix.py` 校验表结构，并要求产品代码中的
`behavior-decision: <decision>` 标记引用本表已有决策。没有标记的非占位
specification manifest 条目直接失败。

一个完整 decision 只能通过 `rwir_catalog.def` 中恰好一条 myrwircap mapping 进入
canonical BinRWIR。mapping 必须使用显式稳定 operation identifier；数组下标、文件顺序、
声明顺序和生成顺序都不能定义 operation identity。缺失、重复或歧义 mapping 是编译错误。

| Decision | Behavior | C | Python | Rust | Go | V8/TypeScript | Agreement | kvlang decision | Boundary fixture | Invalid input | Design record |
|---|---|---|---|---|---|---|---|---|---|---|---|
| integer-no-float-roundtrip | Integer calculation does not pass through floating point | integer operators stay in integer domains | integer operators stay in integer domains | integer operators stay in integer domains | integer operators stay in integer domains | BigInt operators stay in the BigInt domain | consensus | Keep integer calculation in an integer representation from input through result | values above 2^53 retain exact integer results | reject mixed representations unless that operation has its own matrix row | runtimec principle 7 |
| floating-to-integer-truncation | Floating-point to integer conversion truncates toward zero inside the target range and saturates outside it | representable values truncate; out-of-range is undefined | `int` truncates for representable values | `as` truncates and saturates | conversion truncates; out-of-range is implementation-dependent | `Math.trunc` produces a Number, not a fixed-width integer | split | Truncate toward zero inside the target width; saturate to target width for out-of-range and ±Inf; NaN is a TypeError | `1e300` to `int8` saturates to `INT8_MAX`; `-3.9` becomes `-3` | NaN conversion returns TypeError instead of a numeric result | kvlang #622 4.3.1 |
| signed-integer-division | Division of signed integers with a nonzero divisor | quotient truncates toward zero | quotient floors | quotient truncates toward zero | quotient truncates toward zero | Number division is not integer division | split | Follow C, Rust, and Go: truncate the quotient toward zero | `-7 / 3` becomes `-2` | zero divisor returns `RUNTIMEC_STATUS_INVALID` | runtimec principle 7 |
| mixed-numeric-equality | Equality between integer `3` and floating point `3.0` | integer converts for numeric comparison | equal | explicit conversion required between concrete types | concrete typed values require matching types | equal under numeric equality | split | Follow C numeric conversion and compare exact represented values | `3 == 3.0` is true; `9007199254740993` is never rounded through floating point | reject a comparison when exact conversion cannot be represented | runtimec principle 7 |
| character-result | A character-producing operation returns one language value | `char` is an integer type | one-character string | `char` is a Unicode scalar value | rune is an alias of `int32` | one-code-unit string | split | Follow Python and V8/TypeScript: return a string value | code point U+0041 returns `"A"` | reject an invalid Unicode scalar value | runtimec principle 7 |
| read-write-alias | One call reads and writes the same storage through separate parameters | pointer aliasing can expose writes to later reads | object aliasing exposes mutation | explicit borrowing controls aliasing | slice aliasing exposes writes | object aliasing exposes mutation | split | RWIR permits declared read/write aliasing; later reads observe ordered writes | bind `a` and `a_out` to one array during in-place quicksort | undeclared aliasing returns `RUNTIMEC_STATUS_INVALID` | RWIR Read/Write Alias Rule |
| uint64-to-float-promotion | Unsigned 64-bit integer to floating point conversion uses the unsigned numeric value | conversion reads the unsigned value, not a sign-reinterpreted integer | `int` to `float` keeps the numeric value | `as f64` reads the unsigned value | `float64(uint64)` reads the unsigned value | `Number` or `BigInt` conversion keeps the sign | consensus | Convert `uint64` as an unsigned value, never through `int64` | `2^63 + 1` becomes a positive float, never a negative one | none; every finite `uint64` converts without signed reinterpretation | kvlang #622 4.1.1 |
| int-to-float32-single-rounding | Integer to float32 conversion rounds once to the nearest float32 | direct cast to `float` rounds once | no native float32; arithmetic uses float64 | `as f32` rounds once | `float32(int)` rounds once | `Math.fround` rounds once | split | Round once to the float32 target; never round through float64 | an integer whose f64-then-f32 double rounding differs converts through one f32 rounding | none | kvlang #622 4.1.2 |
| mixed-signed-unsigned-arithmetic | Mixed signed and unsigned arithmetic and bitwise operations | usual arithmetic conversions apply | arbitrary-precision integers mix freely | mixed operands require an explicit cast | mixed operands require an explicit conversion | `Number` and `BigInt` do not mix | split | Arithmetic and bitwise operations reject mixed signs; comparisons use exact mathematical values | `u64 + i64` and `u64 & i64` are rejected; comparing `-1` with `UINT64_MAX` makes `-1` smaller | mixed arithmetic or bitwise operands return `RUNTIMEC_STATUS_INVALID`; untyped literal adaptation is unaffected | kvlang #622 4.1.3 |
| float-division-by-zero | Floating-point division by zero | IEEE 754 infinities and NaN | raises `ZeroDivisionError` | IEEE 754 infinities and NaN | IEEE 754 infinities and NaN | IEEE 754 `Infinity` and `NaN` | split | Follow C, Rust, Go, and V8/TypeScript: IEEE 754 infinities and NaN | `1.0 / 0.0` is `+inf`; `-1.0 / 0.0` is `-inf`; `0.0 / 0.0` is `NaN` | none; a zero divisor is valid in floating point | kvlang #622 4.1.4 |
| none-criterion | What counts as the None value | a null pointer is typed and distinct from an empty object | `None` is distinct from the empty string | `Option::None` is distinct from empty values | a nil pointer or interface is distinct from empty values | `null` and `undefined` are distinct from the empty string | consensus | None exactly when langtype is None or a pointer type and the body is empty; an empty string is not None | an empty string with string langtype is a normal value; a pointer langtype with empty body is None | None or pointer langtype with a non-empty body returns `RUNTIMEC_STATUS_INVALID` | kvlang #622 4.1.5 |
| unsigned-neg-abs-sign | Unary neg, abs, and sign for unsigned values | neg wraps; abs and sign are ordinary functions | integers are signed by default | neg wraps; unsigned values do not define abs | neg wraps; unsigned values have no abs concept | `BigInt` neg is exact and sign follows the value | split | neg wraps modulo the width; abs is identity; sign reports by unsigned value | `neg(UINT64_MAX)` is `1`; `abs(UINT64_MAX)` is `UINT64_MAX`; `sign(UINT64_MAX)` is positive | none | kvlang #622 4.1.6 |
| float-member-key-identity | Float-to-text used for member keys and display | formatting differs by implementation | `repr` keeps the `.0` and NaN/inf spellings | `Display` drops integral `.0` and prints NaN/inf | formatting drops integral `.0` and prints NaN/Inf | `String(number)` drops integral `.0` and prints NaN/Infinity | split | Shortest round-trip; integral values drop `.0`; `-0.0` normalizes to `0.0`; exponent below `-4` or above `15` uses exponent form with two digits; NaN is rejected as a key while ±Inf is allowed; display uses `nan`/`inf`/`-inf` | `1.0` and `1` share one key; `-0.0` and `0.0` share one key; `Inf` keys are allowed | NaN member key returns `RUNTIMEC_STATUS_INVALID` | kvlang #622 4.3.2 |

## Required workflow

1. Add one complete row with a stable Decision value.
2. Record all five language behaviors; `unknown` and placeholder text are forbidden.
3. Choose `consensus` or `split` and state the exact kvlang behavior.
4. Add a boundary fixture and an invalid-input result.
5. Add `behavior-decision: <decision>` beside the specification or implementation.
6. Only then add executable semantics.

进入 executable semantics 前，还必须通过
`myrwircap-binrwir-mapping.md` 定义的集合覆盖、identifier 唯一性和签名等价检查。

Changing an established row is a language change. The same change must update fixtures and
its issue or design record; silent reinterpretation is forbidden.

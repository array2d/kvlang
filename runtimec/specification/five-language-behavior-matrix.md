# 五语言行为决策矩阵

<!-- runtimec-behavior-matrix-version: 1 -->

本文件是 runtimec 语言行为决策的唯一事实源，不是实现进度表。任何行为进入
`specification/`、`vmkv/`、`vmbin/` 或 `vmjit/` 前，必须先在这里形成完整
决策。五种语言一致时，kvlang 遵从共同直觉；五种语言分裂时，必须明确选择
阵营、边界样例和非法输入结果。

`tools/architecture/check_behavior_matrix.py` 校验表结构，并要求产品代码中的
`behavior-decision: <decision>` 标记引用本表已有决策。没有标记的非占位
specification manifest 条目直接失败。

| Decision | Behavior | C | Python | Rust | Go | V8/TypeScript | Agreement | kvlang decision | Boundary fixture | Invalid input | Design record |
|---|---|---|---|---|---|---|---|---|---|---|---|
| integer-no-float-roundtrip | Integer calculation does not pass through floating point | integer operators stay in integer domains | integer operators stay in integer domains | integer operators stay in integer domains | integer operators stay in integer domains | BigInt operators stay in the BigInt domain | consensus | Keep integer calculation in an integer representation from input through result | values above 2^53 retain exact integer results | reject mixed representations unless that operation has its own matrix row | runtimec principle 7 |
| floating-to-integer-truncation | Finite floating point to integer conversion removes the fractional part toward zero | toward zero within representable range | `int` truncates toward zero | `as` truncates toward zero | conversion removes the fractional part toward zero | `Math.trunc` truncates toward zero | consensus | Truncate the fractional part toward zero | `-3.9` becomes `-3` | reject non-finite and out-of-range values | runtimec principle 7 |
| signed-integer-division | Division of signed integers with a nonzero divisor | quotient truncates toward zero | quotient floors | quotient truncates toward zero | quotient truncates toward zero | Number division is not integer division | split | Follow C, Rust, and Go: truncate the quotient toward zero | `-7 / 3` becomes `-2` | zero divisor returns `RUNTIMEC_STATUS_INVALID` | runtimec principle 7 |
| mixed-numeric-equality | Equality between integer `3` and floating point `3.0` | integer converts for numeric comparison | equal | explicit conversion required between concrete types | concrete typed values require matching types | equal under numeric equality | split | Follow C numeric conversion and compare exact represented values | `3 == 3.0` is true; `9007199254740993` is never rounded through floating point | reject a comparison when exact conversion cannot be represented | runtimec principle 7 |
| character-result | A character-producing operation returns one language value | `char` is an integer type | one-character string | `char` is a Unicode scalar value | rune is an alias of `int32` | one-code-unit string | split | Follow Python and V8/TypeScript: return a string value | code point U+0041 returns `"A"` | reject an invalid Unicode scalar value | runtimec principle 7 |
| read-write-alias | One call reads and writes the same storage through separate parameters | pointer aliasing can expose writes to later reads | object aliasing exposes mutation | explicit borrowing controls aliasing | slice aliasing exposes writes | object aliasing exposes mutation | split | RWIR permits declared read/write aliasing; later reads observe ordered writes | bind `a` and `a_out` to one array during in-place quicksort | undeclared aliasing returns `RUNTIMEC_STATUS_INVALID` | RWIR Read/Write Alias Rule |

## Required workflow

1. Add one complete row with a stable Decision value.
2. Record all five language behaviors; `unknown` and placeholder text are forbidden.
3. Choose `consensus` or `split` and state the exact kvlang behavior.
4. Add a boundary fixture and an invalid-input result.
5. Add `behavior-decision: <decision>` beside the specification or implementation.
6. Only then add executable semantics.

Changing an established row is a language change. The same change must update fixtures and
its issue or design record; silent reinterpretation is forbidden.

# 五语言规范原始文档（对齐参考）

kvlang 行为与命名对齐 C/Python/Rust/Go/TypeScript。此目录存放各语言**规范原始文档**，作为 kvlang 语言规范设计与"分裂时选阵营"裁决的一手依据。仅供参考，不参与构建。

| 语言 | 文档 | 版本/来源 | 形态 |
|------|------|-----------|------|
| C99 | `c99/n1256.pdf` | WG14 N1256（C99 + TC1/2/3，ISO 9899:1999 事实等价终稿） | PDF ~552 页 |
| Rust | `rust/reference-repo/` | github.com/rust-lang/reference @ main（shallow） | mdBook markdown（`src/`） |
| Python | `python/cpython/Doc/reference/` | github.com/python/cpython @ main（blobless + sparse） | reStructuredText |
| Go | `go/go-spec.html` | go.dev/ref/spec（The Go Programming Language Specification） | 单页 HTML |
| TypeScript | `typescript/typescript-1.8-spec.md` | microsoft/TypeScript @ v1.8.10 `doc/spec.md`（官方规范终版，此后停更并归档） | Markdown |

下载日期：2026-09-06。来源均为各语言权威/官方发布点。

> 冻结快照 commit：rust reference @ e24eecf97b0c9a6dbac67191098204dc8a190aaa；cpython @ 7cd63cddade4c3e1de17c3443695afe1f41d8495（已移除 .git，纯文件形态）

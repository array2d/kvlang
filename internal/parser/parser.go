// Package parser 提供 kvlang 语法分析：Token 流 → AST。
//
// 入口:
//   - ParseFile(path) → *ast.File             (文件级)
//   - ParseCode(r) → *ast.File                (io.Reader 级)
//   - ParseLine(line) → *ast.Instruction       (行级)
//   - ParseSignature(sig) → ast.FormalParams   (签名级)
package parser

package parser

import "kvlang/internal/ast"

// ParseFuncSig 将签名字符串解析为 ast.FuncSig。
// 签名格式为 KV 中存储的 FuncSig.String() 输出：def name(A:t) -> (B:t)
func ParseFuncSig(sig string) ast.FuncSig {
	toks := Scan(sig)
	p := &parser{tokens: toks}
	return p.parseFuncSig()
}

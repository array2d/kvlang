package termio

// TermStream 表示一个已解析的终端流配置。
type TermStream struct {
	Type   string // "websocket" | "file" | ""
	Detail string // ws://url 或文件路径
}

// IsZero 终端未配置时返回 true。
func (s TermStream) IsZero() bool { return s.Type == "" }

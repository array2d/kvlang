package kvspace

import "errors"

var (
	// ErrNotFound 在 Get 时 key 不存在，或 Watch 超时时返回。
	ErrNotFound = errors.New("kvspace: key not found")

	// ErrClosed 在连接已关闭后调用任意操作时返回。
	ErrClosed = errors.New("kvspace: connection closed")

	// ErrLinkLoop 在软链接解析超过最大跳数时返回。
	ErrLinkLoop = errors.New("kvspace: link loop detected")
)

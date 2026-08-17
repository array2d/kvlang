package term

import (
	"fmt"
	"os"
	"sync"
)

var (
	fileWriters   = map[string]*os.File{}
	fileWritersMu sync.Mutex
)

// writeFile 追加一行文本到文件（自动添加换行符），委托 writeFileRaw 处理文件打开/写入。
func writeFile(path, text string) error { return writeFileRaw(path, text+"\n") }

// readFile 从文件读取一行文本（去除尾部换行）。
func readFile(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("read %s: %w", path, err)
	}
	s := string(data)
	if len(s) > 0 && s[len(s)-1] == '\n' {
		s = s[:len(s)-1]
	}
	if len(s) > 0 && s[len(s)-1] == '\r' {
		s = s[:len(s)-1]
	}
	return s, nil
}

// writeFileRaw 追加文本到文件（不添加换行）。
func writeFileRaw(path, text string) error {
	fileWritersMu.Lock()
	defer fileWritersMu.Unlock()
	f, ok := fileWriters[path]
	if !ok {
		var err error
		f, err = os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil { return fmt.Errorf("open %s: %w", path, err) }
		fileWriters[path] = f
	}
	if _, err := f.WriteString(text); err != nil { return fmt.Errorf("write %s: %w", path, err) }
	return f.Sync()
}

package rwir

import (
	"context"
	"fmt"
	"strings"

	"github.com/array2d/kvspace-go"
)

// Param 表示一条指令的读写槽：名称 + kind。
type Param struct {
	Name string
	Kind string
}

// Rwir 表示执行层 [addr0, addr1] 解码后的一条 rwir 指令。
type Rwir struct {
	Opcode string  // [addr0, 0] = "+" | OpCall | OpReturn | ...
	Reads  []Param // [addr0, -1], [addr0, -2], ...
	Writes []Param // [addr0, 1], [addr0, 2], ...
}

// maxParams 单条指令最大读写槽数。对齐五语言参数上限：C 标准保证≥127，取 2^7=128。
// Decode 的 MGET key 数 = 1 + 2*maxParams = 257，Redis 单次批量完全可承受。
// 超过 128 槽的指令极少见（函数参数的极端情形），超出时 Decode 返回错误而非静默截断。
const maxParams = 128

// Decode 从 kvspace 执行层 key 解码指令。
// scope 帧：key 名前缀 scope 链（. 连接），用父帧 extindex 解析。
func Decode(ctx context.Context, kv kvspace.KVSpace, linkBase, pc string) (*Rwir, error) {
	lastSlash := strings.LastIndex(pc, "/[")
	if lastSlash < 0 {
		return nil, fmt.Errorf("Decode: invalid pc (no /[coord]): %q", pc)
	}
	addr0 := extractAddr0(pc[lastSlash+1:])

	scopePrefix, lookupBase := scopePrefixAndBase(linkBase)

	names := make([]string, 0, 1+maxParams*2)
	names = append(names, fmt.Sprintf("%s[%d,0]", scopePrefix, addr0))
	for i := 1; i <= maxParams; i++ {
		names = append(names, fmt.Sprintf("%s[%d,-%d]", scopePrefix, addr0, i))
		names = append(names, fmt.Sprintf("%s[%d,%d]", scopePrefix, addr0, i))
	}

	vals := kv.Get(lookupBase, names)

	r := &Rwir{}
	if s := string(vals[0].RawBytes()); s != "" {
		r.Opcode = s
	}
	truncated := false
	for i := 1; i <= maxParams; i++ {
		readIdx := (i-1)*2 + 1
		writeIdx := readIdx + 1
		if readIdx < len(vals) {
			if s := string(vals[readIdx].RawBytes()); s != "" {
				r.Reads = append(r.Reads, Param{Name: s, Kind: vals[readIdx].Kind()})
				if i == maxParams { truncated = true }
			}
		}
		if writeIdx < len(vals) {
			if s := string(vals[writeIdx].RawBytes()); s != "" {
				r.Writes = append(r.Writes, Param{Name: s, Kind: vals[writeIdx].Kind()})
				if i == maxParams { truncated = true }
			}
		}
	}
	if truncated {
		return nil, fmt.Errorf("Decode: instruction at %s exceeds maxParams=%d slots (reads=%d, writes=%d)",
			pc, maxParams, len(r.Reads), len(r.Writes))
	}
	return r, nil
}

// scopePrefixAndBase 从 linkBase 提取 scope 链（. 连接）与 rwfunc 帧 linkBase。
// linkBase="/vthread/1/[0,0]/_a/_b/" → "a.b", "/vthread/1/[0,0]/"
// linkBase="/vthread/1/[0,0]/"      → "",      "/vthread/1/[0,0]/"
// linkBase="/vthread/1/"            → "",      "/vthread/1/"
func scopePrefixAndBase(linkBase string) (scopePrefix, lookupBase string) {
	lookupBase = linkBase
	trimmed := strings.TrimRight(linkBase, "/")
	if trimmed == "" || !strings.Contains(trimmed, "[") {
		return
	}
	var scopes []string
	rest := trimmed
	for rest != "" {
		sep := strings.LastIndex(rest, "/")
		if sep < 0 { break }
		seg := rest[sep+1:]
		rest = rest[:sep]
		if len(seg) > 0 && seg[0] == '[' {
			lookupBase = rest + "/" + seg + "/"
			break
		}
		scopes = append(scopes, seg)
	}
	for i := len(scopes) - 1; i >= 0; i-- {
		if scopePrefix != "" { scopePrefix += "." }
		scopePrefix += scopes[i]
	}
	return
}

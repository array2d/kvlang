package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"

	"strconv")

func cmdKVSpace(args []string) {
	// 全局 FlagSet：解析 --kvspace 及子命令
	fs := flag.NewFlagSet("kvspace", flag.ExitOnError)
	dsn := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang kvspace [--kvspace dsn] <subcommand> [args]")
		fmt.Fprintln(os.Stderr, "subcommands: get mget set del list tree dump watch notify clear trace")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	sub := fs.Args() // 子命令及其参数
	if len(sub) == 0 {
		fs.Usage()
		os.Exit(1)
	}

	kv := kvspace.Conn(*dsn)
	defer kv.DisConn()

	switch sub[0] {
	case "get":
		if len(sub) < 2 { usageExit("kvlang kvspace get <key>") }
		val, err := kv.Get(sub[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		fmt.Println(val) // Value.String() prints "kind:repr" debug format

	case "mget":
		if len(sub) < 2 { usageExit("kvlang kvspace mget <key1> <key2> ...") }
		vals, err := kv.GetMany(sub[1:])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for i, v := range vals {
			if v.IsNil() { fmt.Printf("%s\t(nil)\n", sub[i+1]) } else { fmt.Printf("%s\t%s\n", sub[i+1], v) }
		}

	case "set":
		if len(sub) < 3 { usageExit("kvlang kvspace set <key> <value>") }
		val, err := parseValueArg(sub[2])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		kv.Set(sub[1], val)

	case "del":
		if len(sub) < 2 { usageExit("kvlang kvspace del <key1> [key2 ...]") }
		kv.Del(sub[1:]...)

	case "list":
		if len(sub) < 2 { usageExit("kvlang kvspace list <prefix>") }
		children, err := kv.List(sub[1])
		if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
		for _, c := range children { fmt.Println(c) }

	case "tree":
		if len(sub) < 2 { usageExit("kvlang kvspace tree <prefix>") }
		fmt.Println(sub[1])
		printTree(kv, sub[1], "")

	case "dump":
		if len(sub) < 2 { usageExit("kvlang kvspace dump <prefix>") }
		dumpPrefix(kv, sub[1])

	case "watch":
		kvWatch(kv, sub[1:])

	case "notify":
		if len(sub) < 3 { usageExit("kvlang kvspace notify <key> <value>") }
		if err := kv.Notify(sub[1], kvspace.Str(sub[2])); err != nil {
			fmt.Fprintln(os.Stderr, err); os.Exit(1)
		}

	case "clear":
		clearAll(kv)

	case "trace":
		// trace 监听 /vthread/<vtid>/.debug.pause 上的暂停事件，
		// 将每个事件输出为一行 NDJSON，并自动发送 "step" 使程序继续执行。
		// 用于捕获已开启调试模式（.debug = "step"）的 vthread 的执行轨迹。
		//
		// 用法：kvlang kvspace trace <vtid>
		// 配合：先用 kvlang kvspace set /vthread/<vtid>/.debug "step" 开启调试
		if len(sub) < 2 { usageExit("kvlang kvspace trace <vtid>") }
		kvTrace(kv, sub[1])

	default:
		fmt.Fprintf(os.Stderr, "unknown kvspace subcommand: %s\n\n", sub[0])
		fs.Usage()
		os.Exit(1)
	}
}

// kvWatch 使用独立 FlagSet 解析 --timeout。
func kvWatch(kv kvspace.KVSpace, args []string) {
	fs := flag.NewFlagSet("watch", flag.ExitOnError)
	timeout := fs.Duration("timeout", 0, "等待超时（如 5s、1m）；0 表示永久阻塞")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang kvspace watch [--timeout duration] <key>")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	if fs.NArg() == 0 {
		fs.Usage()
		os.Exit(1)
	}
	key := fs.Arg(0)
	result, err := kv.Watch(key, *timeout)
	if err != nil { fmt.Fprintln(os.Stderr, err); os.Exit(1) }
	fmt.Println(result) // Value.String() prints "kind:repr" debug format
}

func usageExit(msg string) {
	fmt.Fprintln(os.Stderr, "usage:", msg)
	os.Exit(1)
}

// clearAll 清空所有 kvspace 数据。
func clearAll(kv kvspace.KVSpace) {
	allRoots, _ := kv.List("/")
	allRoots = append(allRoots,
		keytree.VthreadRoot[1:],  // 去掉前缀 / 就是根级名称
		keytree.SrcRoot[1:],
		keytree.FuncRoot[1:],
		keytree.SysRoot[1:],
		"dev", "t",
	)
	seen := map[string]bool{}
	for _, c := range allRoots {
		if c == "." || c == "" || seen[c] { continue }
		seen[c] = true
		kv.DelTree("/" + c)
	}
}

func printTree(kv kvspace.KVSpace, prefix, indent string) {
	children, _ := kv.List(prefix)
	for i, c := range children {
		last := i == len(children)-1
		branch := "├── "
		if last { branch = "└── " }
		fmt.Printf("%s%s%s\n", indent, branch, c)
		next := indent + "│   "
		if last { next = indent + "    " }
		printTree(kv, prefix+"/"+c, next)
	}
}

func dumpPrefix(kv kvspace.KVSpace, prefix string) {
	if valV, err := kv.Get(prefix); err == nil && !valV.IsNil() {
		// Value.String() 输出 kind:repr 格式，展示类型和长度
		short := strings.ReplaceAll(valV.String(), "\n", "↵")
		if len(short) > 80 { short = short[:80] + "…" }
		fmt.Printf("%-60s %s\n", prefix, short)
	}
	children, _ := kv.List(prefix)
	for _, c := range children { dumpPrefix(kv, prefix+"/"+c) }
}

// kvTrace 监听 /vthread/<vtid>/.debug.pause 上的暂停事件。
//
// 每收到一条暂停事件（JSON），将其原样输出为一行 NDJSON（stdout），
// 然后自动向 /vthread/<vtid>/.debug.resume 发送 "step"，驱动 CPU 执行下一条指令。
//
// 终止条件：
//   - 连续超时（30 s 无事件）：认为程序已结束
//   - os.Interrupt（Ctrl-C）
//
// 用于配合 kvlang kvspace set /vthread/<vtid>/.debug "step" 捕获程序执行轨迹，
// 输出结果可直接送入 jq / 其他 NDJSON 工具分析。
func kvTrace(kv kvspace.KVSpace, vtid string) {
	pauseKey  := keytree.VThreadDebugPause(vtid)
	resumeKey := keytree.VThreadDebugResume(vtid)
	statusKey := keytree.VThreadStatus(vtid)

	const watchTimeout = 10 * time.Second
	const maxIdle      = 3 // 连续超时次数上限

	idle := 0
	for {
		val, err := kv.Watch(pauseKey, watchTimeout)
		if err != nil {
			// 超时：检查 vthread 是否已终止
			idle++
			statusVal, serr := kv.Get(statusKey)
			if serr != nil || statusVal.IsNil() || idle >= maxIdle {
				// vthread 已终止或长期无活动 → 正常结束 trace
				return
			}
			continue
		}
		idle = 0

		// 将暂停事件输出为一行 NDJSON
		// val 本身是 CPU 投递的 JSON 字符串，直接输出即可
		raw := val.Str()
		if raw == "" {
			// 空通知（不常见），构造最小 JSON
			out, _ := json.Marshal(map[string]any{"vtid": vtid, "note": "empty-pause"})
			raw = string(out)
		}
		fmt.Println(raw)

		// 自动发送 "step"，驱动 CPU 继续执行下一条指令
		kv.Notify(resumeKey, kvspace.Str("step")) //nolint:errcheck
	}
}

// parseValueArg 解析 CLI 的 value 参数为 kvspace.XValue。
//
// 格式：kind:repr（Value.String() 往返格式）
//
//	int:42          → kvspace.Int(42)
//	float:3.14      → kvspace.Float(3.14)
//	bool:true       → kvspace.Bool(true)
//	string:hello    → kvspace.Str("hello")
//	plain (无冒号)   → kvspace.Str(plain)  向后兼容
func parseValueArg(raw string) (kvspace.XValue, error) {
	// 查找第一个冒号作为 kind/repr 分隔符
	idx := strings.Index(raw, ":")
	if idx < 0 {
		// 无冒号：向后兼容，视为 string
		return kvspace.Str(raw), nil
	}
	kind := raw[:idx]
	repr := raw[idx+1:]
	switch kind {
	case "int":
		i, err := strconv.ParseInt(repr, 10, 64)
		if err != nil {
			return kvspace.XValue{}, fmt.Errorf("invalid int value: %q", repr)
		}
		return kvspace.Int(i), nil
	case "float":
		f, err := strconv.ParseFloat(repr, 64)
		if err != nil {
			return kvspace.XValue{}, fmt.Errorf("invalid float value: %q", repr)
		}
		return kvspace.Float(f), nil
	case "bool":
		switch repr {
		case "true":
			return kvspace.Bool(true), nil
		case "false":
			return kvspace.Bool(false), nil
		default:
			return kvspace.XValue{}, fmt.Errorf("invalid bool value: %q (expected true/false)", repr)
		}
	case "string":
		return kvspace.Str(repr), nil
	case "nil":
		return kvspace.XValue{}, nil
	default:
		// 未知 kind（如 tensor:120B）→ Raw 存储
		// bytes kind 的 repr 是十六进制
		return kvspace.Raw(kind, []byte(repr)), nil
	}
}

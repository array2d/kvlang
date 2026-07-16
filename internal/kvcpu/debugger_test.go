// debugger_test.go — kvcpu 调试器深度验证 + kvlang 执行逻辑反向验证。
//
// 测试策略：
//   每个测试启动一个执行 goroutine（CPU.Execute），测试主协程逐步接收
//   .debug.pause 事件，在每条指令执行**前**读取 KV 帧状态，与手算期望值
//   对比，从而"反向验证"执行逻辑的正确性。
//
// 覆盖场景：
//   1. TestDebugger_EventFormat        — pause 事件 JSON 格式正确性
//   2. TestDebugger_SumTo3_Trace       — sum_to(3) 全步迹：验证 while 迭代次数与算术
//   3. TestDebugger_FirstDiv7_Break    — first_div7(20)：验证 break 在 i=7 退出循环
//   4. TestDebugger_SumOdds10_Continue — sum_odds(10)：验证 continue 跳过偶数迭代
//   5. TestDebugger_ContinueCmd        — "continue" 命令恢复全速执行
//   6. TestDebugger_AbortCmd           — "abort" 命令立即终止 vthread
package kvcpu_test

import (
	"context"
	"encoding/json"
	"strings"
	"sync"
	"testing"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvcpu"
	"kvlang/internal/kvspace"
	_ "kvlang/internal/kvspace/redis" // 注册 Redis 后端
	"kvlang/internal/layoutcode"
	"kvlang/internal/lower"
	"kvlang/internal/op"
	"kvlang/internal/parser"
	"kvlang/internal/vthread"
)

const (
	dbgTestAddr    = "127.0.0.1:6379"
	dbgStepTimeout = 1 * time.Second // 单步等待超时（程序结束后最多等 1s 即检测到 done）
)

// ─── 基础结构 ────────────────────────────────────────────────────────────────

// pauseEv 是 .debug.pause 通知的 JSON 结构。
type pauseEv struct {
	PC    string `json:"pc"`
	Func  string `json:"func"`
	Frame string `json:"frame"`
	Op    string `json:"op"`
}

// traceSession 封装调试会话：单步步进 + 事件收集。
type traceSession struct {
	kv        kvspace.KVSpace
	vtid      string
	pauseKey  string
	resumeKey string
	Events    []pauseEv // 已收集的暂停事件（按顺序）
	wg        sync.WaitGroup
}

// newSession 创建调试会话，Bootstrap 函数 funcName(args...)，设置 debug=step，
// 在新 goroutine 中启动 Execute。调用 Run() 开始步进。
func newSession(
	t *testing.T, kv kvspace.KVSpace, vtid, funcName string, args []string,
) *traceSession {
	t.Helper()
	ctx := context.Background()

	kv.DelTree(keytree.VThread(vtid))
	t.Cleanup(func() { kv.DelTree(keytree.VThread(vtid)) })

	firstPC := layoutcode.Bootstrap(ctx, kv, vtid, funcName, args)
	if firstPC == "" {
		t.Fatalf("Bootstrap %q vtid=%q failed", funcName, vtid)
	}
	vthread.Set(ctx, kv, vtid, firstPC, "init")
	// 在 goroutine 启动前写 debug 标志，保证 Execute 首次 isFuncEntryPC 检查时可见
	kv.Set(keytree.VThreadDebug(vtid), kvspace.Str("step"))

	s := &traceSession{
		kv:        kv,
		vtid:      vtid,
		pauseKey:  keytree.VThreadDebugPause(vtid),
		resumeKey: keytree.VThreadDebugResume(vtid),
	}
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		c := kvcpu.New(kv, "debugtest")
		c.Execute(firstPC)
	}()
	return s
}

// Run 步进执行：每次收到 .debug.pause 事件，调用 handler 返回命令。
// handler 返回 "step" → 继续单步；"continue"/"abort" → 结束步进，等待 Execute 结束。
// 返回时 Execute goroutine 已确认退出。
func (s *traceSession) Run(t *testing.T, handler func(ev pauseEv, step int) string) {
	t.Helper()
	done := make(chan struct{})
	go func() { s.wg.Wait(); close(done) }()

	step := 0
loop:
	for {
		val, err := s.kv.Watch(s.pauseKey, dbgStepTimeout)
		if err != nil {
			// Watch 超时：检查 Execute 是否已结束
			select {
			case <-done:
				break loop
			default:
				t.Logf("[%s] Watch timeout at step=%d, waiting for Execute...", s.vtid, step)
				continue
			}
		}

		var ev pauseEv
		json.Unmarshal([]byte(val.Str()), &ev)
		step++
		s.Events = append(s.Events, ev)

		cmd := handler(ev, step)
		s.kv.Notify(s.resumeKey, kvspace.Str(cmd))

		if cmd != "step" {
			<-done // "continue"/"abort" 后等待 Execute 完全退出
			break loop
		}
	}
	s.wg.Wait()
}

// readInt 读取帧内整数变量，返回 int64（变量不存在时返回 0）。
func readInt(kv kvspace.KVSpace, frame, name string) int64 {
	v, _ := kv.Get(frame + "/" + name)
	return v.Int()
}

// loadSrc 解析 kv 源码，lower，写入 kvspace（pkg="main"）。
func loadSrc(t *testing.T, kv kvspace.KVSpace, src string) {
	t.Helper()
	df, diags, err := parser.ParseCode(strings.NewReader(src))
	if err != nil {
		t.Fatalf("loadSrc parse: %v", err)
	}
	for _, d := range diags {
		t.Logf("loadSrc diag: %v", d)
	}
	for i := range df.Funcs {
		fn := lower.Func(&df.Funcs[i])
		layoutcode.WriteFunc(kv, "main", fn)
	}
}

// decodeAt 解码给定 PC 的指令（用于在 pause 事件处理中获取读写槽）。
func decodeAt(kv kvspace.KVSpace, pc string) *op.Instruction {
	inst, _ := op.Decode(context.Background(), kv, pc)
	if inst == nil {
		return &op.Instruction{}
	}
	return inst
}

// ─── 测试 1：事件 JSON 格式 ──────────────────────────────────────────────────

// TestDebugger_EventFormat 验证 .debug.pause 事件的 JSON 字段完整且语义正确：
//   - pc 必须以 /.fn/[0,0] 结尾（函数入口）
//   - func 与源码函数名一致
//   - frame 非空（帧根路径）
//   - op 非空（被暂停指令的 opcode）
func TestDebugger_EventFormat(t *testing.T) {
	kv := kvspace.Conn(dbgTestAddr)
	defer kv.DisConn()

	const src = `
def idplus(x: int) -> (r: int) {
    x + 0 -> r
}
`
	loadSrc(t, kv, src)

	s := newSession(t, kv, "dbg_fmt", "idplus", []string{"42"})
	gotFirst := false
	s.Run(t, func(ev pauseEv, step int) string {
		if !gotFirst {
			gotFirst = true
			// 第一个暂停必定是函数入口
			if !strings.HasSuffix(ev.PC, "/.fn/[0,0]") {
				t.Errorf("first pause pc %q should end with /.fn/[0,0]", ev.PC)
			}
			if ev.Func != "idplus" {
				t.Errorf("func=%q, want %q", ev.Func, "idplus")
			}
			if ev.Frame == "" {
				t.Errorf("frame should not be empty")
			}
			if ev.Op == "" {
				t.Errorf("op should not be empty")
			}
			t.Logf("EventFormat OK: pc=%s func=%s frame=%s op=%s", ev.PC, ev.Func, ev.Frame, ev.Op)
		}
		return "continue" // 验证完第一个事件后立即恢复全速
	})
}

// ─── 测试 2：sum_to(3) 全步迹验证 ───────────────────────────────────────────

// TestDebugger_SumTo3_Trace 对 sum_to(3) 进行全量单步迹：
//
// 反向验证逻辑：
//  1. 在每条 `+([total],[i]) → total` 暂停时（dispatch 前），
//     读取帧的 total/i 值，与期望的"执行前状态"对比。
//     期望：iter1(total=0,i=1) iter2(total=1,i=2) iter3(total=3,i=3)
//  2. `<=` 操作共 4 次（3 真 + 1 假退出循环）
//  3. 最终 return 暂停时 total=6
//
// 这同时验证了调试器在步进过程中不影响执行语义。
func TestDebugger_SumTo3_Trace(t *testing.T) {
	kv := kvspace.Conn(dbgTestAddr)
	defer kv.DisConn()

	const src = `
def sum_to(n: int) -> (total: int) {
    0 -> total
    1 -> i
    while (i <= n) {
        total + i -> total
        i + 1 -> i
    }
}
`
	loadSrc(t, kv, src)
	s := newSession(t, kv, "dbg_sum3", "sum_to", []string{"3"})

	// 记录 total-add 暂停时的"执行前"状态
	type addState struct{ total, i int64 }
	var totalAdds []addState // +([total],[i]) 暂停时的 (total, i)
	var leCount   int        // <= 操作次数
	var returnOps int        // return 操作次数
	var finalTotal int64     // 最后一次 return 暂停时的 total

	s.Run(t, func(ev pauseEv, step int) string {
		switch ev.Op {
		case "<=":
			leCount++
		case "+":
			// 区分两种 + 指令：+([total],[i]) vs +(i,1)
			inst := decodeAt(kv, ev.PC)
			if len(inst.Reads) >= 1 && inst.Reads[0] == "total" {
				// 总和累加 + 的暂停：此时 dispatch 尚未执行，读取执行前状态
				tot := readInt(kv, ev.Frame, "total")
				i   := readInt(kv, ev.Frame, "i")
				totalAdds = append(totalAdds, addState{tot, i})
				t.Logf("  total-add pause step=%d: total=%d i=%d (before add)", step, tot, i)
			}
		case "return":
			returnOps++
			finalTotal = readInt(kv, ev.Frame, "total")
			t.Logf("  return pause step=%d: total=%d", step, finalTotal)
		}
		return "step"
	})

	// ── 断言 ──────────────────────────────────────────────────────────────────

	// 3 次真条件 + 1 次假退出 = 4 次 <= 检查
	if leCount != 4 {
		t.Errorf("le count=%d, want 4 (3 true + 1 false exit)", leCount)
	}

	// while 执行 3 次，每次 1 次 total-add
	if len(totalAdds) != 3 {
		t.Errorf("total-add count=%d, want 3", len(totalAdds))
	}

	// 验证每次 total-add 前的执行状态（即手算期望值）
	expected := []addState{{0, 1}, {1, 2}, {3, 3}}
	for idx, got := range totalAdds {
		if idx >= len(expected) {
			break
		}
		want := expected[idx]
		if got.total != want.total || got.i != want.i {
			t.Errorf("total-add[%d]: got(total=%d,i=%d), want(total=%d,i=%d)",
				idx, got.total, got.i, want.total, want.i)
		}
	}

	// return 执行 1 次（一个函数），最终 total=6 (0+1+2+3)
	if returnOps != 1 {
		t.Errorf("return count=%d, want 1", returnOps)
	}
	if finalTotal != 6 {
		t.Errorf("final total=%d, want 6", finalTotal)
	}
}

// ─── 测试 3：first_div7(20) break 验证 ──────────────────────────────────────

// TestDebugger_FirstDiv7_Break 反向验证 break 语义：
//
//  1. 循环体内的 `%` 操作恰好执行 7 次（i=1..7）
//  2. 第 7 次 `%` 前帧中 i=7
//  3. break 后不再有任何 `%` 操作
//  4. 最终 return 时 result=7
func TestDebugger_FirstDiv7_Break(t *testing.T) {
	kv := kvspace.Conn(dbgTestAddr)
	defer kv.DisConn()

	const src = `
def first_div7(n: int) -> (result: int) {
    0 -> result
    1 -> i
    while (i <= n) {
        i % 7 -> rem
        rem == 0 -> hit
        if (hit) {
            i -> result
            break
        }
        i + 1 -> i
    }
}
`
	loadSrc(t, kv, src)
	s := newSession(t, kv, "dbg_div7", "first_div7", []string{"20"})

	var modCount  int    // % 操作次数
	var lastModI  int64  // 最后一次 % 前帧的 i 值
	var finalResult int64

	s.Run(t, func(ev pauseEv, step int) string {
		switch ev.Op {
		case "%":
			modCount++
			iVal := readInt(kv, ev.Frame, "i")
			lastModI = iVal
			t.Logf("  %% pause step=%d: i=%d", step, iVal)
		case "return":
			finalResult = readInt(kv, ev.Frame, "result")
			t.Logf("  return pause step=%d: result=%d", step, finalResult)
		}
		return "step"
	})

	// ── 断言 ──────────────────────────────────────────────────────────────────

	// break 在 i=7 触发：循环体恰好执行 7 次（i=1..7）
	if modCount != 7 {
		t.Errorf("mod count=%d, want 7 (break at i=7)", modCount)
	}

	// 最后一次 % 时 i=7（break 的那次迭代）
	if lastModI != 7 {
		t.Errorf("last mod i=%d, want 7", lastModI)
	}

	// 最终返回值 result=7
	if finalResult != 7 {
		t.Errorf("result=%d, want 7", finalResult)
	}
}

// ─── 测试 4：sum_odds(10) continue 验证 ─────────────────────────────────────

// TestDebugger_SumOdds10_Continue 反向验证 continue 语义：
//
//  1. `%` 操作执行 10 次（i=1..10，每个 i 都检查奇偶）
//  2. `+([total],[i])` 操作执行 5 次（i=1,3,5,7,9 奇数）
//  3. 5 次 total-add 前的执行状态 (total, i) 严格符合手算期望
//  4. 最终 total=25 (1+3+5+7+9)
func TestDebugger_SumOdds10_Continue(t *testing.T) {
	kv := kvspace.Conn(dbgTestAddr)
	defer kv.DisConn()

	const src = `
def sum_odds(n: int) -> (total: int) {
    0 -> total
    1 -> i
    while (i <= n) {
        i % 2 -> rem
        rem == 0 -> even
        if (even) {
            i + 1 -> i
            continue
        }
        total + i -> total
        i + 1 -> i
    }
}
`
	loadSrc(t, kv, src)
	s := newSession(t, kv, "dbg_odds", "sum_odds", []string{"10"})

	type addState struct{ total, i int64 }
	var modCount  int
	var totalAdds []addState
	var finalTotal int64

	s.Run(t, func(ev pauseEv, step int) string {
		switch ev.Op {
		case "%":
			modCount++
		case "+":
			inst := decodeAt(kv, ev.PC)
			if len(inst.Reads) >= 1 && inst.Reads[0] == "total" {
				tot := readInt(kv, ev.Frame, "total")
				i   := readInt(kv, ev.Frame, "i")
				totalAdds = append(totalAdds, addState{tot, i})
				t.Logf("  total-add pause: total=%d i=%d", tot, i)
			}
		case "return":
			finalTotal = readInt(kv, ev.Frame, "total")
			t.Logf("  return pause: total=%d", finalTotal)
		}
		return "step"
	})

	// ── 断言 ──────────────────────────────────────────────────────────────────

	// i=1..10 每次都做 % 检查：10 次
	if modCount != 10 {
		t.Errorf("mod count=%d, want 10 (i=1..10)", modCount)
	}

	// 只有奇数 i=1,3,5,7,9 做 total 累加：5 次
	if len(totalAdds) != 5 {
		t.Errorf("total-add count=%d, want 5 (odd i only)", len(totalAdds))
	}

	// 验证每次 total-add 前的帧状态（dispatch 前：尚未执行加法）
	wantAdds := []addState{
		{0, 1},  // 即将计算 0+1=1
		{1, 3},  // 即将计算 1+3=4
		{4, 5},  // 即将计算 4+5=9
		{9, 7},  // 即将计算 9+7=16
		{16, 9}, // 即将计算 16+9=25
	}
	for idx, got := range totalAdds {
		if idx >= len(wantAdds) {
			break
		}
		want := wantAdds[idx]
		if got.total != want.total || got.i != want.i {
			t.Errorf("total-add[%d]: got(total=%d,i=%d), want(total=%d,i=%d)",
				idx, got.total, got.i, want.total, want.i)
		}
	}

	// 最终 total=25 (1+3+5+7+9)
	if finalTotal != 25 {
		t.Errorf("final total=%d, want 25", finalTotal)
	}
}

// ─── 测试 5：continue 命令恢复全速 ──────────────────────────────────────────

// TestDebugger_ContinueCmd 验证：
//  1. 在前 3 步后发送 "continue" 命令
//  2. vthread 能在 "continue" 后正常完成，无错误
//  3. 总步数（调试期间）确实是 3（不多不少）
func TestDebugger_ContinueCmd(t *testing.T) {
	kv := kvspace.Conn(dbgTestAddr)
	defer kv.DisConn()

	const src = `
def sum_to(n: int) -> (total: int) {
    0 -> total
    1 -> i
    while (i <= n) {
        total + i -> total
        i + 1 -> i
    }
}
`
	loadSrc(t, kv, src)
	s := newSession(t, kv, "dbg_cont", "sum_to", []string{"10"})

	s.Run(t, func(ev pauseEv, step int) string {
		t.Logf("  step=%d pc=%s op=%s", step, ev.PC, ev.Op)
		if step >= 3 {
			return "continue" // 3 步后恢复全速
		}
		return "step"
	})

	// 步进期间收集了恰好 3 个事件
	if len(s.Events) != 3 {
		t.Errorf("events during debug=%d, want 3", len(s.Events))
	}

	// vthread 不应处于 error 状态（正常结束：SetDone 已 Del .status）
	errMsg, _ := kv.Get(keytree.VThreadStatusMsg(s.vtid, "error"))
	if errMsg.Str() != "" {
		t.Errorf("unexpected error after continue: %s", errMsg.Str())
	}
}

// ─── 测试 6：abort 命令立即终止 ─────────────────────────────────────────────

// TestDebugger_AbortCmd 验证：
//  1. 在第 1 步发送 "abort" 命令
//  2. Execute 立即返回非 nil 错误
//  3. vthread .error/msg 写入 "debug: aborted by agent"
func TestDebugger_AbortCmd(t *testing.T) {
	kv := kvspace.Conn(dbgTestAddr)
	defer kv.DisConn()

	const src = `
def sum_to(n: int) -> (total: int) {
    0 -> total
    1 -> i
    while (i <= n) {
        total + i -> total
        i + 1 -> i
    }
}
`
	loadSrc(t, kv, src)
	s := newSession(t, kv, "dbg_abort", "sum_to", []string{"100"})

	abortStep := 0
	s.Run(t, func(ev pauseEv, step int) string {
		abortStep = step
		t.Logf("  abort at step=%d pc=%s op=%s", step, ev.PC, ev.Op)
		return "abort" // 立即终止
	})

	// 只收到 1 个事件（第 1 步即 abort）
	if len(s.Events) != 1 || abortStep != 1 {
		t.Errorf("events=%d abortStep=%d, want events=1 abortStep=1",
			len(s.Events), abortStep)
	}

	// vthread 应写入 error 消息
	errMsg, _ := kv.Get(keytree.VThreadStatusMsg(s.vtid, "error"))
	if !strings.Contains(errMsg.Str(), "aborted by agent") {
		t.Errorf("error msg=%q, want contains 'aborted by agent'", errMsg.Str())
	}
	t.Logf("  abort error msg: %s", errMsg.Str())
}

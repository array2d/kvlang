package kvcpu

import (
	"context"
	"strings"
	"time"

	"kvlang/internal/keytree"
	"kvlang/internal/kvspace"
	"kvlang/internal/logx"
)

// pick 扫描 /vthread/ 下状态为 init 的虚线程，原子抢占并返回其绝对 PC。
// 若无可用 vthread，返回 ""。
func (c *cpu) pick(ctx context.Context) string {
	vtids, err := c.kv.List(keytree.VthreadRoot)
	if err != nil {
		logx.Debug("pick: list error: %v", err)
		return ""
	}
	for _, vtid := range vtids {
		// 跳过元数据键（seq / ready），以及以 . 开头的保留项
		if vtid == "seq" || vtid == "ready" || strings.HasPrefix(vtid, ".") {
			continue
		}

		statusVal, err := c.kv.Get(keytree.VThreadStatus(vtid))
		if err != nil || statusVal.Str() != "init" {
			continue
		}

		// 原子抢占：status → running
		c.kv.Set(keytree.VThreadStatus(vtid), kvspace.Str("running"))

		// 读取绝对 PC
		pcVal, _ := c.kv.Get(keytree.VThreadPC(vtid))
		pc := pcVal.Str()
		if pc == "" {
			// 兜底：PC 尚未写入时从 vtid 构造
			pc = keytree.VThreadSlot(vtid, "", 0, 0)
		}
		logx.Debug("pick: claimed vtid=%s pc=%s", vtid, pc)
		return pc
	}
	return ""
}

// wait 阻塞等待新 vthread 就绪通知（BLPOP /vthread/ready）。
// 超时或 ctx 取消后返回。
func (c *cpu) wait(ctx context.Context) {
	val, err := c.kv.Watch(keytree.VthreadReady, 5*time.Second)
	if err != nil {
		if ctx.Err() == nil && !strings.Contains(err.Error(), "nil") {
			logx.Debug("wait: BLPOP: %v", err)
		}
		return
	}
	if s := val.Str(); s != "" {
		logx.Debug("wait: notify vtid=%s", s)
	}
}

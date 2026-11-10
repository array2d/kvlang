package builtin

import (
	"time"

	"github.com/array2d/kvspace-go"
	"oldhero/rwir"
	"oldhero/vthread"
)

func init() {
	Register("time.now", "rwir time.now() -> (T:time)", timeNowOp{})

	// time.sub returns Duration
	Register("time.sub", "rwir time.sub(A:time, B:time) -> (C:duration)", timeSubOp{})

	// time.add: time + duration → time
	Register("time.add", "rwir time.add(A:time, B:duration) -> (C:time)", timeAddOp{})

	// time/duration.nanos / millis / seconds / minutes / hours: int64 → duration
	Register("time/duration.nanos",   "rwir time/duration.nanos(N:int64) -> (D:duration)",   durFromOp{scale: 1})
	Register("time/duration.millis",  "rwir time/duration.millis(N:int64) -> (D:duration)",  durFromOp{scale: 1e6})
	Register("time/duration.seconds", "rwir time/duration.seconds(N:int64) -> (D:duration)", durFromOp{scale: 1e9})
	Register("time/duration.minutes", "rwir time/duration.minutes(N:int64) -> (D:duration)", durFromOp{scale: 60e9})
	Register("time/duration.hours",   "rwir time/duration.hours(N:int64) -> (D:duration)",   durFromOp{scale: 3600e9})

	// time/duration.as_nanos / as_millis / … : duration → int64
	Register("time/duration.as_nanos",   "rwir time/duration.as_nanos(D:duration) -> (N:int64)",   durToOp{div: 1})
	Register("time/duration.as_millis",  "rwir time/duration.as_millis(D:duration) -> (N:int64)",  durToOp{div: 1e6})
	Register("time/duration.as_seconds", "rwir time/duration.as_seconds(D:duration) -> (N:int64)", durToOp{div: 1e9})
	Register("time/duration.as_minutes", "rwir time/duration.as_minutes(D:duration) -> (N:int64)", durToOp{div: 60e9})
	Register("time/duration.as_hours",   "rwir time/duration.as_hours(D:duration) -> (N:int64)",   durToOp{div: 3600e9})

	// time/duration arith
	Register("time/duration.add", "rwir time/duration.add(A:duration, B:duration) -> (C:duration)", durArithOp{sub: false})
	Register("time/duration.sub", "rwir time/duration.sub(A:duration, B:duration) -> (C:duration)", durArithOp{sub: true})

	// time/duration cmp
	Register("time/duration.before", "rwir time/duration.before(A:duration, B:duration) -> (C:bool)", durCmpOp{before: true})
	Register("time/duration.after",  "rwir time/duration.after(A:duration, B:duration) -> (C:bool)",  durCmpOp{before: false})

	// time cmp
	Register("time.before", "rwir time.before(A:time, B:time) -> (C:bool)", timeCmpOp{before: true})
	Register("time.after",  "rwir time.after(A:time, B:time) -> (C:bool)",  timeCmpOp{before: false})
}

// ── time.now ────────────────────────────────────────────────────────────

type timeNowOp struct{}

func (timeNowOp) Call(f *rwir.Frame) error {
	return writeResult(f, kvspace.NewTime(time.Now().UnixNano()))
}

// ── time.sub: time - time → duration ─────────────────────────────────────

type timeSubOp struct{}

func (timeSubOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time.sub requires 2 time args")
		return nil
	}
	a, b := asInt64(inputs[0]), asInt64(inputs[1])
	return writeResult(f, kvspace.NewDuration(a-b))
}

// ── time.add: time + duration → time ─────────────────────────────────────

type timeAddOp struct{}

func (timeAddOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time.add requires time and duration")
		return nil
	}
	t, d := asInt64(inputs[0]), asInt64(inputs[1])
	return writeResult(f, kvspace.NewTime(t+d))
}

// ── time/duration.nanos / millis / seconds / minutes / hours ──────────────

type durFromOp struct{ scale int64 }

func (o durFromOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time/duration.from requires 1 int64 arg")
		return nil
	}
	return writeResult(f, kvspace.NewDuration(asInt64(inputs[0])*o.scale))
}

// ── time/duration.as_nanos / as_millis / … ────────────────────────────────

type durToOp struct{ div int64 }

func (o durToOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time/duration.as requires 1 duration arg")
		return nil
	}
	return writeResult(f, kvspace.NewInt64(asInt64(inputs[0])/o.div))
}

// ── time/duration arith ──────────────────────────────────────────────────

type durArithOp struct{ sub bool }

func (o durArithOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time/duration arith requires 2 duration args")
		return nil
	}
	a, b := asInt64(inputs[0]), asInt64(inputs[1])
	if o.sub { a -= b } else { a += b }
	return writeResult(f, kvspace.NewDuration(a))
}

// ── time / time/duration cmp ─────────────────────────────────────────────

type timeCmpOp struct{ before bool }

func (o timeCmpOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time cmp requires 2 args")
		return nil
	}
	a, b := asInt64(inputs[0]), asInt64(inputs[1])
	var result bool
	if o.before { result = a < b } else { result = a > b }
	return writeResult(f, kvspace.NewBool(result))
}

type durCmpOp struct{ before bool }

func (o durCmpOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 2 {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: time/duration cmp requires 2 duration args")
		return nil
	}
	a, b := asInt64(inputs[0]), asInt64(inputs[1])
	var result bool
	if o.before { result = a < b } else { result = a > b }
	return writeResult(f, kvspace.NewBool(result))
}

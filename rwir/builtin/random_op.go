package builtin

import (
	"github.com/array2d/kvspace-go"
	"kvlang/rwir"
	"kvlang/vthread"
	rand "kvlang/rwir/builtin/random"
)

func init() {
	Register("random.uint64", "rwir random.uint64() -> (N:uint64)", randomUint64Op{})
	Register("random.int63", "rwir random.int63() -> (N:int64)", randomInt63Op{})
	Register("random.intn", "rwir random.intn(N:int64) -> (R:uint64)", randomIntnOp{})
}

type randomUint64Op struct{}

func (randomUint64Op) Call(f *rwir.Frame) error {
	return writeResult(f, rand.Uint64())
}

type randomInt63Op struct{}

func (randomInt63Op) Call(f *rwir.Frame) error {
	return writeResult(f, rand.Int63())
}

type randomIntnOp struct{}

func (randomIntnOp) Call(f *rwir.Frame) error {
	inputs := readInputs(f)
	if len(inputs) < 1 || kvspace.IsNone(inputs[0]) {
		vthread.SetError(bg, f.KV, f.Vtid, f.PC, "TypeError: random.intn requires 1 int64 arg")
		return nil
	}
	return writeResult(f, rand.Intn(uint64(asInt64(inputs[0]))))
}

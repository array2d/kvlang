package builtin

func evalBinaryInt(inputs []nativeValue, fn func(int64, int64) int64) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	return nativeValue{kind: "int", i: fn(inputs[0].asInt(), inputs[1].asInt())}, nil
}

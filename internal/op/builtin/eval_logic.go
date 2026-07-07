package builtin

func evalLogic(inputs []nativeValue, fn func(bool, bool) bool) (nativeValue, error) {
	if err := requireBinary(inputs); err != nil {
		return nativeValue{}, err
	}
	return nativeValue{kind: "bool", b: fn(inputs[0].asBool(), inputs[1].asBool())}, nil
}

func evalNot(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	return nativeValue{kind: "bool", b: !inputs[0].asBool()}, nil
}

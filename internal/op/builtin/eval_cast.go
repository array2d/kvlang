package builtin

func evalToInt(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	v := inputs[0]
	switch v.kind {
	case "int":
		return v, nil
	case "float":
		return nativeValue{kind: "int", i: int64(v.f)}, nil
	case "bool":
		if v.b {
			return nativeValue{kind: "int", i: 1}, nil
		}
		return nativeValue{kind: "int", i: 0}, nil
	default:
		return nativeValue{kind: "int", i: v.asInt()}, nil
	}
}

func evalToFloat(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	v := inputs[0]
	switch v.kind {
	case "float":
		return v, nil
	case "int":
		return nativeValue{kind: "float", f: float64(v.i)}, nil
	case "bool":
		if v.b {
			return nativeValue{kind: "float", f: 1.0}, nil
		}
		return nativeValue{kind: "float", f: 0.0}, nil
	default:
		return nativeValue{kind: "float", f: v.asFloat()}, nil
	}
}

func evalToBool(inputs []nativeValue) (nativeValue, error) {
	if err := requireUnary(inputs); err != nil {
		return nativeValue{}, err
	}
	return nativeValue{kind: "bool", b: inputs[0].asBool()}, nil
}

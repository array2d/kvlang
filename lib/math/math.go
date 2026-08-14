// Package math 提供数学内置常量 key（可读写数据），写入 /lib/math.Pi 等。
package math

import (
	"math"

	"github.com/array2d/kvspace-go"
	"kvlang/lib"
)

func init() {
	lib.RegisterLibData("math.Pi",  kvspace.NewFloat64(math.Pi))
	lib.RegisterLibData("math.E",   kvspace.NewFloat64(math.E))
	lib.RegisterLibData("math.Tau", kvspace.NewFloat64(2*math.Pi))
}

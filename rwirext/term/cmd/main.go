// Command term rwirext 以独立进程启动 print/println/cerr/input 外部执行器。
package main

import (
	"flag"

	"github.com/array2d/kvspace-go"
	_ "github.com/array2d/kvspace-go/redis"
	"kvlang/rwirext/term"
)

func main() {
	dsn := flag.String("kvspace", "redis://127.0.0.1:6379", "kvspace DSN")
	flag.Parse()

	kv := kvspace.Conn(*dsn)
	defer kv.DisConn()

	term.Serve(kv)
}

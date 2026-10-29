// Command json rwirext 以独立进程启动 json.to/json.from 外部执行器。
package main

import (
	"flag"

	"extruntime_tutorial/go/json"
)

func main() {
	dsn := flag.String("kvspace", "redis://127.0.0.1:6379", "kvspace DSN")
	flag.Parse()

	json.Serve(*dsn)
}

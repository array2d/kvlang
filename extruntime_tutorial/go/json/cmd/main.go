
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

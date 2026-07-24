package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"sort"
	"strconv"

	"kvlang/internal/keytree"
	"kvlang/internal/vthread"
	"github.com/array2d/kvspace-go"
)

func cmdPS(args []string) {
	fs := flag.NewFlagSet("ps", flag.ExitOnError)
	dsn := fs.String("kvspace", defaultKVSpace(), kvspaceFlagDesc)
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: kvlang ps [--kvspace <dsn>]")
		fmt.Fprintln(os.Stderr, "  list all vthreads like Linux ps")
		fs.PrintDefaults()
	}
	fs.Parse(args)

	kv := kvspace.Conn(*dsn)

	vtids := kv.List(keytree.VthreadRoot + keytree.PathSegSep)
	if len(vtids) == 0 {
		return
	}

	var ids []int64
	for _, v := range vtids {
		if id, e := strconv.ParseInt(v, 10, 64); e == nil {
			ids = append(ids, id)
		}
	}
	sort.Slice(ids, func(i, j int) bool { return ids[i] < ids[j] })

	fmt.Printf("%-20s %-8s %s\n", "PID", "STATUS", "PC")
	for _, id := range ids {
		vtid := strconv.FormatInt(id, 10)
		pc, status := vthread.Get(context.Background(), kv, vtid)
		if status == "" {
			status = "-"
		}
		fmt.Printf("%-20s %-8s %s\n", vtid, status, pc)
	}
}

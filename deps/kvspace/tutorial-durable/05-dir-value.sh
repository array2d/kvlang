#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /dv/
$KV deltree /dv2/

echo "=== dir with value and child ==="
$KV mkindex /dv/
$KV set /dv int:99
$KV set /dv/x int:1
$KV get /dv
$KV list /dv/
$KV get /dv/x
$KV mkindex /dv/x/
$KV set /dv/x/y int:2
$KV get /dv/x/y
$KV list /dv/x/

echo "== del value keeps children ==="
$KV mkindex /dv2/
$KV set /dv2/x int:42
$KV del /dv2
$KV get /dv2/x
$KV list /dv2/





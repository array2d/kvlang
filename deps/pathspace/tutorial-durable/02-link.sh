#!/bin/bash

set -e
KV="${PATHSPACE_BIN:-pathspace}"
$KV deltree /tgt/
$KV deltree /lnk

$KV mkindex /tgt/
$KV set /tgt/x int:42
$KV set /tgt/y int:7
$KV set /tgt/z string:hello
$KV set /lnk "*:lib:/tgt/"

echo "=== read through ==="
$KV get /lnk/x /lnk/y

echo "=== list through ==="
$KV list /lnk/

echo "=== write through ==="
$KV set /lnk/w int:99
$KV get /tgt/w
$KV list /tgt/

echo "=== del through link ==="
$KV del /lnk/y
$KV get /tgt/y
$KV list /tgt/

echo "=== del link body ==="
$KV del /lnk
$KV get /lnk
$KV get /tgt/x



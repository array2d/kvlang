#!/bin/bash

set -e
KV="${PATHSPACE_BIN:-pathspace}"
$KV deltree /tgt/
$KV deltree /base/
$KV deltree /merge/
$KV deltree /lnk

echo "=== unlink link ==="
$KV mkindex /tgt/
$KV set /tgt/x int:1
$KV set /lnk "*:lib:/tgt/"
$KV get /lnk/x
$KV del /lnk
$KV get /lnk
$KV get /tgt/x

echo "=== unlink extindex ==="
$KV mkindex /base/
$KV set /base/a int:2
$KV extindex /merge/ /base/
$KV set /merge/b int:3
$KV delextindex /merge/
$KV get /merge
$KV get /base/a



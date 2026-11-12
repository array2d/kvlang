#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /base/
$KV deltree /merge/

$KV mkindex /base/
$KV set /base/a int:1
$KV set /base/b int:2
$KV set /base/c int:3
$KV extindex /merge/ /base/

echo "=== read fallthrough ==="
$KV get /merge/a /merge/b /merge/c

echo "=== write new key ==="
$KV set /merge/z string:upper_only
$KV get /merge/z
$KV get /base/z

echo "=== list merge ==="
$KV list /merge/

echo "=== del upper key ==="
$KV del /merge/z
$KV get /merge/z
$KV list /merge/

echo "=== nested write ==="
$KV mkindex /merge/nest/
$KV set /merge/nest/x int:42
$KV set /merge/nest/y int:43
$KV get /merge/nest/x /merge/nest/y
$KV get /base/nest/x
$KV list /merge/nest/

echo "=== deltree preserves base ==="
$KV deltree /merge/
$KV get /merge/a
$KV get /base/a
$KV list /base/





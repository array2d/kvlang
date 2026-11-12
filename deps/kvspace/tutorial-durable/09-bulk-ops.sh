#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /b/

$KV mkindex /b/
$KV mkindex /b/a/
$KV mkindex /b/b/

echo "=== bulk get ==="
$KV set /b/a/x int:1
$KV set /b/a/y int:2
$KV set /b/b/z int:3
$KV get /b/a/x /b/a/y /b/b/z

echo "=== bulk del siblings ==="
$KV mkindex /b/c/
$KV set /b/c/x int:1
$KV set /b/c/y int:2
$KV set /b/c/z int:3
$KV del /b/c/x /b/c/y
$KV get /b/c/x /b/c/y /b/c/z

echo "=== bulk del cross dirs ==="
$KV mkindex /b/d/
$KV mkindex /b/e/
$KV mkindex /b/f/
$KV set /b/d/a int:4
$KV set /b/e/b int:5
$KV set /b/f/c int:6
$KV del /b/d/a /b/e/b
$KV get /b/d/a /b/e/b /b/f/c
$KV list /b/f/





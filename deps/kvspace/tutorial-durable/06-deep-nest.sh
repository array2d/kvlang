#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /deep/

echo "=== deep tree ==="
$KV mkindex /deep/
$KV mkindex /deep/a/
$KV mkindex /deep/a/b/
$KV mkindex /deep/a/b/c/
$KV mkindex /deep/a/b/c/d/
$KV mkindex /deep/a/b/c/d/e/
$KV set /deep/a/b/c/d/e/f int:6
$KV get /deep/a/b/c/d/e/f
$KV list /deep/
$KV set /deep/a/b/c int:3
$KV get /deep/a/b/c
$KV list /deep/a/b/c/
$KV list /deep/a/b/

echo "=== deltree mid ==="
$KV deltree /deep/a/b/c/d/e/
$KV get /deep/a/b/c/d/e/f
$KV get /deep/a/b/c/d/e
$KV get /deep/a/b/c

echo "=== deltree leaf ==="
$KV deltree /deep/a/b/
$KV get /deep/a/b/c
$KV list /deep/







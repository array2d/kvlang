#!/bin/bash

set -e
KV="${PATHSPACE_BIN:-pathspace}"
$KV deltree /e/
$KV mkindex /e/

echo "=== overwrite kind ==="
$KV set /e/v int:42
$KV get /e/v
$KV set /e/v string:hello
$KV get /e/v

echo "=== empty list ==="
$KV list /e/empty/

echo "=== del non-existent ==="
$KV del /e/ghost
$KV get /e/ghost

echo "=== deltree non-existent ==="
$KV deltree /e/nonexist/




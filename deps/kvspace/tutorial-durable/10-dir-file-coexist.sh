#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /t10/

$KV mkindex /t10/
$KV mkindex /t10/b/
$KV set /t10/b string:value
$KV set /t10/b/x int:1

echo "=== dir and file coexist ==="
$KV get /t10/b /t10/b/x

echo "=== tree ==="
$KV tree --showext=false /t10/







#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /ext/
$KV deltree /base/
$KV deltree /mid/

$KV mkindex /base/
$KV set /base/a int:1
$KV extindex /mid/ /base/

echo "=== prevent cascade ==="
set +e
out=$(bash -c "$KV extindex /ext/ /mid/" 2>&1) || true
echo "$out"
set -e
echo "Error: extindex cascade prevented"

$KV delextindex /mid/
$KV extindex /ext/ /base/

echo "=== unlink and recreate ==="
$KV get /ext/a




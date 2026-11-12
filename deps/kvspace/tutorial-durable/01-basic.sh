#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /t01/

echo "=== Set & Get ==="
$KV mkindex /t01/
$KV set /t01/a int:42
$KV get /t01/a

echo "=== Set & List ==="
$KV set /t01/b int:7
$KV set /t01/c string:hello
$KV list /t01/

echo "=== Get bulk ==="
$KV get /t01/a /t01/b /t01/c

echo "=== Get nil ==="
$KV get /t01/nonexist

echo "=== Del ==="
$KV del /t01/a
$KV get /t01/a
$KV list /t01/





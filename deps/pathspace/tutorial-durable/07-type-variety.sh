#!/bin/bash

set -e
KV="${PATHSPACE_BIN:-pathspace}"
$KV deltree /t/
$KV mkindex /t/

echo "=== int ==="
$KV set /t/zero int:0
$KV set /t/neg int:-9223372036854775808
$KV set /t/pos int:9223372036854775807
$KV get /t/zero /t/neg /t/pos

echo "=== float ==="
$KV set /t/f0 float:0
$KV set /t/fneg float:-3.141592653589793
$KV set /t/fpos float:2.718281828459045
$KV get /t/f0 /t/fneg /t/fpos

echo "=== bool ==="
$KV set /t/yes bool:true
$KV set /t/no bool:false
$KV get /t/yes /t/no

echo "=== string ==="
$KV set /t/empty string:
$KV set /t/space "string:hello world"
$KV set /t/unicode string:αβγ
$KV get /t/empty /t/space /t/unicode

echo "=== nil ==="
$KV set /t/nothing nil:
$KV get /t/nothing




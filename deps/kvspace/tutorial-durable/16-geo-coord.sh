#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /geo/

echo "=== geo list ==="
$KV set /geo 'map:[float64,float64]·[]char/utf8'   # declare container (memhead) before member writes
$KV set '/geo·[39.9,116.4]' 'string:Beijing'
$KV set '/geo·[31.23,121.47]' 'string:Shanghai'
$KV list /geo· --kind --showext=false

echo "=== get ==="
$KV get '/geo·[39.9,116.4]' '/geo·[31.23,121.47]'

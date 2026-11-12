#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /m/
$KV deltree /h/

echo "=== map head ==="
$KV set /m 'map:[int64,int64]·float32'
$KV head /m

echo "=== map list ==="
$KV set '/m·[0,1]' 'float32:3.14'
$KV set '/m·[1,2]' 'float32:6.28'
$KV set '/m·[0,0]' 'float32:1.0'
$KV list /m· --kind --showext=false

echo "=== get ==="
$KV get '/m·[1,2]' '/m·[9,9]'

echo "=== 命名成员 vs 坐标成员 ==="
$KV set /h 'map:[]char/utf8·int64'   # declare container (memhead) explicitly
$KV set /h·x 'int:1'
$KV set '/h·[0]' 'int:2'
$KV list /h· --kind=false --showext=false

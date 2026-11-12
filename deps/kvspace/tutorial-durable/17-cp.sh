#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /src/
$KV deltree /dst/
$KV deltree /p/
$KV deltree /q/
$KV deltree /d1/
$KV deltree /d2/
$KV deltree /base/
$KV deltree /realext/
$KV deltree /ov/

echo "=== cp single key ==="
$KV set /src/one int:100
$KV cp /src/one /dst/one
$KV get /dst/one

echo "=== cpdir memindex container ==="
$KV set /p 'map:[]char/utf8·float64'   # container value (memhead) required for `·` members
$KV set /p·x float:3.14
$KV set /p·y int:42
$KV cpdir /p /q
$KV list "/q·"

echo "=== cpdir /-children subtree ==="
$KV mkindex /d1/
$KV set /d1/a int:1
$KV set /d1/b int:2
$KV cpdir /d1 /d2
$KV get /d2/a /d2/b

echo "=== cpdir extindex → new overlay on target ==="
$KV mkindex /realext/
$KV set /realext/a int:1
$KV set /realext/b int:2
$KV mkindex /base/
$KV extindex /base/ /realext/
$KV cpdir /base /ov
$KV get /ov/a /ov/b

echo "=== mutate ext, visible via copied overlay ==="
$KV set /realext/a int:777
$KV get /ov/a

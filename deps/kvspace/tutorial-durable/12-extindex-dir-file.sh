#!/bin/bash

set -e
KV="${KVSPACE_BIN:-kvspace}"
$KV deltree /ext/
$KV deltree /base/

$KV mkindex /base/
$KV set /base/k int:1
$KV extindex /ext/ /base/
$KV mkindex /ext/d/
$KV set /ext/d string:val
$KV set /ext/d/y int:2

echo "=== get dir value and child ==="
$KV get /ext/d /ext/d/y /ext/k

echo "=== tree shows extindex with dir+file ==="
$KV tree /ext/







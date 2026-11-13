#!/bin/bash

set -e
KV="${PATHSPACE_BIN:-pathspace}"
$KV deltree /ext/
$KV deltree /base/

$KV mkindex /base/
$KV set /base/a int:1
$KV extindex /ext/ /base/
$KV set /ext/z int:42
$KV mkindex /ext/x/
$KV set /ext/x/z int:99

echo "=== tree showext true ==="
$KV tree /ext/

echo "=== tree showext false ==="
$KV tree --showext=false /ext/








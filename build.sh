#!/bin/bash
set -e

# kvlang Build Script
# 编译结果输出到 /tmp 目录
# Go 安装路径: ~/sdk/go

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="/tmp/kvlang"
GOROOT="$HOME/sdk/go"
export PATH="$GOROOT/bin:$PATH"
export GOPROXY="${GOPROXY:-https://goproxy.cn,direct}"

echo "=== kvlang Builder ==="
echo "Go version: $(go version)"
echo "Source dir: $SCRIPT_DIR"
echo "Output dir: $OUTPUT_DIR"

mkdir -p "$OUTPUT_DIR"

cd "$SCRIPT_DIR"

# 下载依赖
echo ""
echo "[1/4] Downloading dependencies..."
go mod tidy

# 运行测试
echo ""
echo "[2/4] Running unit tests..."
go test ./... -v -count=1 -run "^Test[^I]" 2>&1 || echo "(tests skipped or failed - continuing)"

# 构建 VM
echo ""
echo "[3/4] Building VM binary..."
go build -ldflags="-s -w" -o "$OUTPUT_DIR/kvlang" ./cmd/vm/

# 构建 loader
echo ""
echo "[4/4] Building loader binary..."
go build -ldflags="-s -w" -o "$OUTPUT_DIR/loader" ./cmd/loader/

echo ""
echo "=== Build Complete ==="
echo "Binaries:"
ls -lh "$OUTPUT_DIR/kvlang" "$OUTPUT_DIR/loader" 2>/dev/null || true

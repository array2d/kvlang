#!/usr/bin/env bash
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
GO_MIN=1.24
GO_FULL=1.25.8                                # version to download if go not installed or too old
WORKDIR="${KVWORKDIR:-$HOME/kvlang-tutorial}"
INSTALL_PREFIX="$HOME/.local"

say()  { echo -e "${GREEN}==>${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
die()  { echo -e "${RED}[X]${NC} $*" >&2; exit 1; }

# ── detect os ──
if ! grep -qi ubuntu /etc/os-release 2>/dev/null; then
    die "this script targets ubuntu; $(cat /etc/os-release 2>/dev/null | head -1) detected"
fi
say "ubuntu detected"

# ── sudo check ──
sudo -v

# ═══════════════════════════════════════════════
# step 1: install dependencies
# ═══════════════════════════════════════════════

# ── go 1.24+ ──
need_go=0
if ! command -v go &>/dev/null; then
    need_go=1
else
    go_ver=$(go version | grep -oP 'go\K[0-9]+\.[0-9]+' | head -1)
    if [ "$(printf '%s\n' "$GO_MIN" "$go_ver" | sort -V | head -1)" != "$GO_MIN" ]; then
        warn "go $go_ver < $GO_MIN, will upgrade"
        need_go=1
    fi
fi
if [ "$need_go" -eq 1 ]; then
    say "installing go $GO_MIN+"
    GO_TAR="go${GO_FULL}.linux-amd64.tar.gz"
    if [ ! -f "/tmp/$GO_TAR" ]; then
        curl -fsSL "https://go.dev/dl/$GO_TAR" -o "/tmp/$GO_TAR"
    fi
    sudo rm -rf /usr/local/go
    sudo tar -C /usr/local -xzf "/tmp/$GO_TAR"
    if ! grep -q '/usr/local/go/bin' "$HOME/.profile" 2>/dev/null; then
        echo 'export PATH=/usr/local/go/bin:$PATH' >> "$HOME/.profile"
    fi
    export PATH=/usr/local/go/bin:$PATH
fi
say "go $(go version | grep -oP 'go[0-9.]+')"

# ── redis-server ──
if ! command -v redis-server &>/dev/null; then
    say "installing redis-server"
    sudo apt-get update -qq
    sudo apt-get install -y -qq redis-server
fi
say "redis-server $(redis-server --version | grep -oP 'v=[0-9.]+')"

# ── python3 ──
if ! command -v python3 &>/dev/null; then
    say "installing python3"
    sudo apt-get update -qq
    sudo apt-get install -y -qq python3
fi
say "python3 $(python3 --version)"

# ═══════════════════════════════════════════════
# step 2: clone repos
# ═══════════════════════════════════════════════

mkdir -p "$WORKDIR"
cd "$WORKDIR"

# clone kvspace-go first (kvlang depends on it via go.mod replace)
if [ ! -d kvspace-go ]; then
    say "cloning kvspace-go"
    git clone --depth 1 git@github.com:array2d/kvspace-go.git
else
    say "kvspace-go already exists, git pull"
    git -C kvspace-go pull --ff-only
fi

if [ ! -d kvlang ]; then
    say "cloning kvlang"
    git clone --depth 1 git@github.com:array2d/kvlang.git
else
    say "kvlang already exists, git pull"
    git -C kvlang pull --ff-only
fi

# ═══════════════════════════════════════════════
# step 3: build (kvspace-go first, then kvlang)
# ═══════════════════════════════════════════════

say "building kvspace-go"
cd "$WORKDIR/kvspace-go"
GOPROXY="$GOPROXY" PREFIX="$INSTALL_PREFIX" make build
export PATH="$INSTALL_PREFIX/bin:$PATH"

say "building kvlang"
cd "$WORKDIR/kvlang"
GOPROXY="$GOPROXY" PREFIX="$INSTALL_PREFIX" make build
export PATH="$INSTALL_PREFIX/bin:$PATH"

# ═══════════════════════════════════════════════
# step 4: start a dedicated test redis
# ═══════════════════════════════════════════════

KVLANG_TEST_REDIS_PORT="${KVLANG_TEST_REDIS_PORT:-16379}"
if redis-cli -p "$KVLANG_TEST_REDIS_PORT" ping &>/dev/null; then
    die "port $KVLANG_TEST_REDIS_PORT is already running Redis; choose a dedicated port with KVLANG_TEST_REDIS_PORT"
fi
say "starting dedicated redis-server on port $KVLANG_TEST_REDIS_PORT"
redis-server \
    --bind 127.0.0.1 \
    --port "$KVLANG_TEST_REDIS_PORT" \
    --save '' \
    --appendonly no \
    --daemonize yes \
    --loglevel warning
trap 'redis-cli -p "$KVLANG_TEST_REDIS_PORT" shutdown nosave &>/dev/null || true' EXIT
sleep 1
redis-cli -p "$KVLANG_TEST_REDIS_PORT" ping &>/dev/null || die "dedicated redis-server failed to start"
say "dedicated redis is up"

# ═══════════════════════════════════════════════
# step 5: run tutorial tests
# ═══════════════════════════════════════════════

say "running tutorial/test.py"
cd "$WORKDIR/kvlang"
KVLANG_KVSPACE="redis://127.0.0.1:$KVLANG_TEST_REDIS_PORT" \
KVLANG_TEST_REDIS=1 \
python3 tutorial/test.py

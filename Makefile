# kvlang 统一构建：产物全部输出到 bin/，各组件独立编译。
#   make runtime    C runtime 库（libkvlang_runtime.so，经 CMake）
#   make runtime-rs 功能完整 Rust runtime → 编译为 kvlang（term/json/http/kvlanglayout 就地 rwir）
#   make layout     Rust layout（kvlanglayout）
#   make json       Go json 扩展（json-rwirext 可执行文件）
#   make oldhero    Go 旧 runtime（kvlang-go，兼容保留，即将归档）
#   make test       全量 tutorial 回归
#   make install    一次性安装产物到最终态目录（.so→/usr/lib，可执行→/usr/bin，头→/usr/include/kvlang/）
#   make all        全部（runtime + runtime-rs + layout + json）
#   make clean      清理 bin/ 与各构建目录
#   make clear      install 的逆操作：删系统目录里 kvspace/kvlang 的 .so 与可执行
#   make status     列出系统目录里已安装的库 / 可执行 / 头
# kvspace 后端由 libkvspace dispatch 前端按 DSN 运行时选择，不再编译期切换。

BIN         := bin

.PHONY: all runtime runtime-rs layout json oldhero test install clean \
        status clear clear-headers clear-all

all: runtime runtime-rs layout json

test: all
	python3 tutorial/test.py --no-build

runtime:
	cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
	cmake --build build/runtime --target kvlang_runtime -j

runtime-rs:
	cargo build --release --manifest-path runtime-rs/Cargo.toml
	cp runtime-rs/target/release/kvlang $(BIN)/kvlang

layout:
	cargo build --release --manifest-path layout/Cargo.toml
	cp layout/target/release/kvlanglayout $(BIN)/
	cp layout/target/release/libkvlanglayout.so $(BIN)/

install:
	install -d /usr/lib /usr/bin /usr/include/kvlang
	install -m 755 $(BIN)/libkvlang_runtime.so /usr/lib/
	install -m 755 layout/target/release/libkvlanglayout.so /usr/lib/
	install -m 755 $(BIN)/kvlang $(BIN)/kvlanglayout /usr/bin/
	install -m 644 runtime/include/kvlang_vthread.h runtime/include/kvlang_runtime.h /usr/include/kvlang/

json:
	cd runtime-rwirext_example/go/json && CGO_LDFLAGS="-lkvspace" go build -o ../../../bin/json-rwirext ./cmd/

oldhero:
	cd oldhero && go build -ldflags="-s -w" -o ../bin/kvlang-go ./cmd/kvlang/

clean:
	rm -rf $(BIN) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path runtime-rs/Cargo.toml

# ── 安装态清理（install 的逆操作，kvspace / kvlang 生态） ─────────────────
#   make clear         删 .so（/usr/lib、/usr/lib/kvspace、/lib、/usr/local/lib …）+ 可执行
#   make clear-headers 再删头（/usr/include/kvspace、/usr/include/kvlang …）
#   make clear-all     clear + clear-headers
# 注：/lib、/bin 多为指向 /usr 的符号链接，重复删同一文件无副作用。
SUDO    ?= sudo
LIBDIRS := /usr/lib/kvspace /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib /usr/local/lib
BINDIRS := /usr/bin /bin /usr/local/bin
BINS    := kvlang kvlanglayout kvspace
HDRDIRS := /usr/include/kvspace /usr/include/kvlang /usr/local/include/kvspace /usr/local/include/kvlang
PCDIRS  := /usr/lib/x86_64-linux-gnu/pkgconfig /usr/lib/pkgconfig /usr/local/lib/pkgconfig

status:
	@echo "── 库 ──"
	@for d in $(LIBDIRS); do ls -1 $$d/libkvspace*.so* $$d/libkvlang*.so* 2>/dev/null | sed 's|^|  |'; done; true
	@for d in $(PCDIRS); do [ -e $$d/kvspace.pc ] && echo "  $$d/kvspace.pc"; done; true
	@echo "── 可执行 ──"
	@for d in $(BINDIRS); do for b in $(BINS); do [ -e $$d/$$b ] && echo "  $$d/$$b"; done; done; true
	@echo "── 头 ──"
	@for d in $(HDRDIRS); do [ -e $$d ] && echo "  $$d"; done; true

clear:
	@for d in $(LIBDIRS); do $(SUDO) rm -f $$d/libkvspace*.so* $$d/libkvlang*.so*; done
	@for d in $(PCDIRS); do $(SUDO) rm -f $$d/kvspace.pc; done
	@for d in $(BINDIRS); do for b in $(BINS); do $(SUDO) rm -f $$d/$$b; done; done
	@$(SUDO) ldconfig 2>/dev/null || true
	@echo "✅ clear：已删 kvspace/kvlang 的 .so 与可执行"

clear-headers:
	@$(SUDO) rm -rf $(HDRDIRS)
	@echo "✅ clear-headers：已删头文件"

clear-all: clear clear-headers

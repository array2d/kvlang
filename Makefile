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
# kvspace 后端由 libkvspace dispatch 前端按 DSN 运行时选择，不再编译期切换。

BIN         := bin

.PHONY: all runtime runtime-rs layout json oldhero test install clean

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
	cargo build --release --manifest-path layout/Cargo.toml --example kvlanglayout
	cargo build --release --manifest-path layout/Cargo.toml
	cp layout/target/release/examples/kvlanglayout $(BIN)/

install:
	install -d /usr/lib /usr/bin /usr/include/kvlang
	install -m 755 $(BIN)/libkvlang_runtime.so /usr/lib/
	install -m 755 layout/target/release/libkvlanglayout.so /usr/lib/
	install -m 755 $(BIN)/kvlang $(BIN)/kvlanglayout /usr/bin/
	install -m 644 runtime/include/kvlang_runtime.h runtime/include/kvlang_rwirext.h /usr/include/kvlang/

json:
	cd runtime-rwirext_example/go/json && CGO_LDFLAGS="-lkvspace" go build -o ../../../bin/json-rwirext ./cmd/

oldhero:
	cd oldhero && go build -ldflags="-s -w" -o ../bin/kvlang-go ./cmd/kvlang/

clean:
	rm -rf $(BIN) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path runtime-rs/Cargo.toml

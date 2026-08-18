# kvlang 统一构建：产物全部输出到 bin/，各组件独立编译。
#   make runtime   C runtime 库（libkvlang_runtime.so，经 CMake）
#   make term      Rust term 扩展（term 可执行文件，独立进程）
#   make run       测试执行器（run，链接 runtime）
#   make layout    Rust layout（layout_file）
#   make json      Go json 扩展（json-rwirext 可执行文件）
#   make oldhero   Go 旧 runtime（kvlang，兼容保留）
#   make test      全量 tutorial 回归（C runtime + Rust term，递归跑所有子目录 kv）
#   make all       全部
#   make clean     清理 bin/ 与各构建目录

BIN         := bin
KVSPACE_LIB ?= kvspace-c

.PHONY: all runtime term run layout json oldhero test clean

all: runtime term run layout json

test: KVSPACE_LIB := kvspace_durable
test: runtime term run layout
	python3 tutorial/test.py --no-build

runtime:
	cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release -DKVSPACE_LIB=$(KVSPACE_LIB)
	cmake --build build/runtime --target kvlang_runtime -j

term:
	cargo build --release --manifest-path runtime-rwirext_example/rust/term/Cargo.toml
	cp runtime-rwirext_example/rust/term/target/release/term $(BIN)/

run: runtime
	cmake --build build/runtime --target run -j

layout:
	KVLANG_KVSPACE_LIB=$(KVSPACE_LIB) cargo build --release --manifest-path layout/Cargo.toml --example layout_file
	cp layout/target/release/examples/layout_file $(BIN)/

json:
	cd runtime-rwirext_example/go/json && go build -o ../../../bin/json-rwirext ./cmd/

oldhero:
	cd oldhero && go build -ldflags="-s -w" -o ../bin/kvlang ./cmd/kvlang/

clean:
	rm -rf $(BIN) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path runtime-rwirext_example/rust/term/Cargo.toml

# kvlang 统一构建：产物全部输出到 bin/，各组件独立编译。
#   make runtime   C runtime 库（libkvlang_runtime.so，经 CMake）
#   make term      Rust term 扩展（term 可执行文件，独立进程）
#   make run       测试执行器（run，链接 runtime）
#   make layout    Rust layout（layout_file）
#   make json      Go json 扩展（json-rwirext 可执行文件）
#   make oldhero   Go 旧 runtime（kvlang，兼容保留）
#   make shm       链 kvspace-c 后端（SHM，默认，= all 的后端组件）
#   make durable   链 kvspace_durable 后端（redis/fs/s3）
#   make test      durable 后端全量 tutorial 回归
#   make all       全部（shm 后端 + json）
#   make clean     清理 bin/ 与各构建目录
# 切换后端只需 make shm / make durable：runtime 经 cmake 重配、layout 经环境变量重建。

BIN         := bin
KVSPACE_LIB ?= kvspace-c

.PHONY: all runtime term run layout json oldhero shm durable test clean

all: runtime term run layout json

shm: runtime term run layout

durable: KVSPACE_LIB := kvspace_durable
durable: runtime term run layout

test: durable
	python3 tutorial/test.py --no-build

runtime:
	cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release -DKVSPACE_LIB=$(KVSPACE_LIB)
	cmake --build build/runtime --target kvlang_runtime -j

term:
	KVLANG_KVSPACE_LIB=$(KVSPACE_LIB) cargo build --release --manifest-path runtime-rwirext_example/rust/term/Cargo.toml
	cp runtime-rwirext_example/rust/term/target/release/term $(BIN)/

run: runtime
	cmake --build build/runtime --target run -j

layout:
	KVLANG_KVSPACE_LIB=$(KVSPACE_LIB) cargo build --release --manifest-path layout/Cargo.toml --example layout_file
	cp layout/target/release/examples/layout_file $(BIN)/

json:
ifeq ($(KVSPACE_LIB),kvspace_durable)
	cd runtime-rwirext_example/go/json && CGO_LDFLAGS="-L$(CURDIR)/../kvspace-durable/target/release -lkvspace_durable -Wl,--disable-new-dtags -Wl,-rpath,$(CURDIR)/../kvspace-durable/target/release" go build -o ../../../bin/json-rwirext ./cmd/
else
	cd runtime-rwirext_example/go/json && CGO_LDFLAGS="-L$(CURDIR)/../kvspace-c/build -lkvspace-c -Wl,--disable-new-dtags -Wl,-rpath,$(CURDIR)/../kvspace-c/build -Wl,-rpath,$(CURDIR)/../blockmalloc/build -Wl,-rpath,$(CURDIR)/../slotsboxmalloc/build" go build -o ../../../bin/json-rwirext ./cmd/
endif

oldhero:
	cd oldhero && go build -ldflags="-s -w" -o ../bin/kvlang ./cmd/kvlang/

clean:
	rm -rf $(BIN) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path runtime-rwirext_example/rust/term/Cargo.toml

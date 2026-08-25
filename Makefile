# kvlang 统一构建：产物全部输出到 bin/，各组件独立编译。
#   make runtime   C runtime 库（libkvlang_runtime.so，经 CMake）
#   make term      Rust term 扩展 → 编译为 kvlang（极简 Rust runtime，替代旧 Go 单体）
#   make layout    Rust layout（layout_file）
#   make json      Go json 扩展（json-rwirext 可执行文件）
#   make oldhero   Go 旧 runtime（kvlang-go，兼容保留，即将归档）
#   make shm       链 kvspace-c 后端（SHM，默认，= all 的后端组件）
#   make durable   链 kvspace_durable 后端（redis/fs/s3）
#   make test      durable 后端全量 tutorial 回归
#   make install   一次性安装产物到最终态目录（.so→/usr/lib，可执行→/usr/bin，头→/usr/include/kvlang/）
#   make all       全部（shm 后端 + json）
#   make clean     清理 bin/ 与各构建目录
# 切换后端只需 make shm / make durable：runtime 经 cmake 重配、layout 经环境变量重建。

BIN         := bin
KVSPACE_LIB ?= kvspace-c

.PHONY: all runtime term layout json oldhero shm durable test install clean

all: runtime term layout json

shm: runtime term layout

durable: KVSPACE_LIB := kvspace_durable
durable: runtime term layout

test: durable
	python3 tutorial/test.py --no-build

runtime:
	cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release -DKVSPACE_LIB=$(KVSPACE_LIB)
	cmake --build build/runtime --target kvlang_runtime -j

term:
	KVLANG_KVSPACE_LIB=$(KVSPACE_LIB) cargo build --release --manifest-path runtime-rwirext_example/rust/term/Cargo.toml
	cp runtime-rwirext_example/rust/term/target/release/kvlang $(BIN)/kvlang

layout:
	KVLANG_KVSPACE_LIB=$(KVSPACE_LIB) cargo build --release --manifest-path layout/Cargo.toml --example layout_file
	KVLANG_KVSPACE_LIB=$(KVSPACE_LIB) cargo build --release --manifest-path layout/Cargo.toml
	cp layout/target/release/examples/layout_file $(BIN)/

install:
	install -d /usr/lib /usr/bin /usr/include/kvlang
	install -m 755 $(BIN)/libkvlang_runtime.so /usr/lib/
	install -m 755 layout/target/release/libkvlang_layout.so /usr/lib/
	install -m 755 $(BIN)/kvlang $(BIN)/layout_file /usr/bin/
	install -m 644 runtime/include/kvlang_runtime.h runtime/include/kvlang_rwirext.h /usr/include/kvlang/

json:
ifeq ($(KVSPACE_LIB),kvspace_durable)
	cd runtime-rwirext_example/go/json && CGO_LDFLAGS="-L$(CURDIR)/../kvspace-durable/target/release -lkvspace_durable -Wl,--disable-new-dtags -Wl,-rpath,$(CURDIR)/../kvspace-durable/target/release" go build -o ../../../bin/json-rwirext ./cmd/
else
	cd runtime-rwirext_example/go/json && CGO_LDFLAGS="-L$(CURDIR)/../kvspace-c/build -lkvspace-c -Wl,--disable-new-dtags -Wl,-rpath,$(CURDIR)/../kvspace-c/build -Wl,-rpath,$(CURDIR)/../blockmalloc/build -Wl,-rpath,$(CURDIR)/../slotsboxmalloc/build" go build -o ../../../bin/json-rwirext ./cmd/
endif

oldhero:
	cd oldhero && go build -ldflags="-s -w" -o ../bin/kvlang-go ./cmd/kvlang/

clean:
	rm -rf $(BIN) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path runtime-rwirext_example/rust/term/Cargo.toml

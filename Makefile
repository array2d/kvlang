# kvlang 统一构建：各自包含在 repo 内（可执行 bin/，库 lib/，后端 lib/kvspace/），
# 不装 /usr；各组件独立编译。
#   make runtime    C runtime 库（libkvlang_runtime.so，经 CMake）——可选 A/B 臂
#   make runtimec2  c2 runtime 库（libkvlang_runtimec2.so，经 CMake）——默认 runtime
#   make byteseek   功能完整 Rust runtime → 编译为 byteseek（term/json/http/kvlanglayout/networld 就地 rwir）
#   make layout     Rust layout（kvlanglayout）
#   make json       Go json 扩展（json-rwirext 可执行文件）
#   make oldhero    Go 旧 runtime（kvlang-go，兼容保留，即将归档）
#   make test       全量 tutorial 回归
#   make all        全部（runtimec2 + byteseek + layout + json；C 版 runtime 不默认构建）
#   make clean      清理 bin/ 与各构建目录
#   make clear      install 的逆操作：删系统目录里 kvspace/kvlang 的 .so 与可执行
#   make status     列出系统目录里已安装的库 / 可执行 / 头
# kvspace 后端由 libkvspace dispatch 前端按 DSN 运行时选择，不再编译期切换。

# 默认 runtime = c2（A/B 目标达成后主选）。C 版仍可 `make runtime` 单独构建。
# 两级布局（repo 即 prefix）：可执行 → bin/，库 → lib/（后端在 lib/kvspace/）。
BIN         := bin
LIB         := lib
BACKEND     := lib/kvspace

# 产物后缀：Linux=.so macOS=.dylib（uname 判 Darwin，跨平台确定）。
ifeq ($(shell uname -s),Darwin)
C2_LIB     := libkvlang_runtimec2.dylib
LAYOUT_LIB := libkvlanglayout.dylib
else
C2_LIB     := libkvlang_runtimec2.so
LAYOUT_LIB := libkvlanglayout.so
endif

.PHONY: all runtime runtimec2 byteseek layout json oldhero test clean \
        status clear clear-headers clear-all single \
        kvspace-libs check-kvspace-head

all: runtimec2 byteseek layout json

test: all
	python3 tutorial/test.py --no-build

# 单一二进制（#493）：把 .so 作为字节嵌进 byteseek，启动时 memfd+dlopen，前端用
# RTLD_NOLOAD 复用已加载的后端。payload 固定组装到 build/embed（不读构建 env）。
# FRONTEND_SO 是构建期参数（不是运行时环境变量）：本分支构建的 libkvspace.so.1。
FRONTEND_SO ?=
# 单一二进制（#493）：默认 embed c2 runtime。C 版 A/B 臂：`make runtime` 后
# 手动替换 lib/libkvlang_runtime.so 再 stage_embed（tools/stage_embed.sh 已同时带 c2）。
single: runtimec2 layout
	./tools/stage_embed.sh $(FRONTEND_SO) build/embed $(CURDIR)
	cargo build --release --manifest-path byteseek/Cargo.toml --features embed
	cp byteseek/target/release/byteseek $(BIN)/byteseek

runtime:
	cmake -S runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
	cmake --build build/runtime --target kvlang_runtime -j

# c2 runtime（默认）：产物 lib/libkvlang_runtimec2.so + bin/binircompiler
# （编译模块的可执行入口）。C 版 libkvlang_runtime.so 仍由 `make runtime` 单独产出。
runtimec2:
	cmake -S runtimec2 -B build/runtimec2 -DCMAKE_BUILD_TYPE=Release
	cmake --build build/runtimec2 --target kvlang_runtimec2 binircompiler -j

# stdlib 是 byteseek 的**编译期输入**：build.rs 用 include_str! 把 stdlib/**/*.kv
# 逐个编进 EMBEDDED_KV。而 cargo 的 rerun-if-changed 只覆盖单次 build，
# 跨目录（stdlib 在仓库根、byteseek 在子目录）不会触发——于是「改了 stdlib、
# 忘了重建 byteseek」时，二进制里跑的还是旧 stdlib，且**没有任何提示**。
# 这条依赖把它变成「改了就必须重跑」：不需要每次无条件重编，改了就一定重编。
BYTESEEK_STDLIB_SOURCES := $(shell find stdlib -name '*.kv')
BYTESEEK_BINARY := $(BIN)/byteseek

byteseek: $(BYTESEEK_BINARY)

$(BYTESEEK_BINARY): $(BYTESEEK_STDLIB_SOURCES) byteseek/Cargo.toml
	cargo build --release --manifest-path byteseek/Cargo.toml
	mkdir -p $(BIN)
	cp byteseek/target/release/byteseek $(BIN)/byteseek

# ── kvspace 三库 + 使用它们的三个可执行 ────────────────────────────────────
# 为什么要有这个目标：head 是跨进程 ABI，四个部件（前端 / shm 后端 / durable 后端 /
# 使用它们的可执行）必须**按同一份布局**构建。只重建其中一个，就会出现「能跑、读
# 到错值」或「invalid XValue」——症状指向错的地方，排查一次要半小时。
# 所以这里一次做完：构建三库 → 装到 lib/ 与 lib/kvspace/ → 用探针验证 16 字节 head。
#
#   make kvspace-libs   构建三库 + 重出 layout/byteseek/kvspace CLI + 探针验证
#   make check-kvspace-head   只跑探针（不改任何文件）
#
# 三个源仓库按约定是 kvlang 的兄弟目录；缺任何一个就明确失败，不静默跳过。
KVSPACE_SRC    ?= ../kvspace
KVSPACE_C_SRC  ?= ../kvspace-c
KVSPACE_DUR_SRC ?= ../kvspace-durable

kvspace-libs: kvspace-frontend kvspace-backend kvspace-durable-lib
	$(MAKE) layout byteseek
	$(MAKE) kvspace-cli
	$(MAKE) check-kvspace-head

kvspace-frontend:
	@test -d "$(KVSPACE_SRC)" || { \
	  echo "缺少 kvspace 源仓库：$(KVSPACE_SRC)（用 KVSPACE_SRC=... 指定）"; exit 1; }
	cmake -S "$(KVSPACE_SRC)" -B build/kvspace -DCMAKE_BUILD_TYPE=Release
	cmake --build build/kvspace -j
	mkdir -p $(LIB)
	cp -P build/kvspace/libkvspace.so* $(LIB)/

kvspace-backend:
	@test -d "$(KVSPACE_C_SRC)" || { \
	  echo "缺少 kvspace-c 源仓库：$(KVSPACE_C_SRC)（用 KVSPACE_C_SRC=... 指定）"; exit 1; }
	cmake -S "$(KVSPACE_C_SRC)" -B build/kvspace-c -DCMAKE_BUILD_TYPE=Release
	cmake --build build/kvspace-c -j
	mkdir -p $(BACKEND)
	cp -P build/kvspace-c/libkvspace-c.so* $(BACKEND)/

kvspace-durable-lib:
	@test -d "$(KVSPACE_DUR_SRC)" || { \
	  echo "缺少 kvspace-durable 源仓库：$(KVSPACE_DUR_SRC)（用 KVSPACE_DUR_SRC=... 指定）"; exit 1; }
	cargo build --release --manifest-path "$(KVSPACE_DUR_SRC)/Cargo.toml"
	mkdir -p $(BACKEND)
	cp -P "$(KVSPACE_DUR_SRC)/target/release/libkvspace_durable.so" $(BACKEND)/libkvspace_durable.so.1

# kvspace CLI 由前端仓的 CMake 目标产出（cli/ 独立 Cargo 项目），装到 bin/。
kvspace-cli: kvspace-frontend
	mkdir -p $(BIN)
	cp "$(KVSPACE_SRC)/cli/target/release/kvspace" $(BIN)/kvspace

# 探针：从**已装**的 libkvspace 里产一个 XValue，检查 head 是 16 字节、字段落在
# 正确偏移。装错版本时在构建期就报错，而不是等到测试期看到一片 invalid XValue。
check-kvspace-head:
	@tmp=$$(mktemp -d); \
	printf '%s\n' \
	  '#include <kvspace/xvalue_head.h>' \
	  '#include <stdio.h>' \
	  '#include <stdlib.h>' \
	  'int main(void) {' \
	  '  uint8_t *o = NULL; uint64_t n = 0;' \
	  '  if (kvspaceXvalueHeadNewScalar(' \
	  '        (const uint8_t *)"\x2a\0\0\0\0\0\0\0", 8, "int64",' \
	  '        &o, &n) != 0) { puts("ctor failed"); return 1; }' \
	  '  int bad = 0;' \
	  '  if (KVSPACE_XVALUE_HEAD_LEN != 16) { puts("HEAD_LEN != 16"); bad = 1; }' \
	  '  if (n != 16 + 8 + 1 + 2 + 5) { printf("total=%llu, want 32\\n", (unsigned long long)n); bad = 1; }' \
	  '  if (o[0] != 8 || o[1] != 0) { puts("bodylen != 8"); bad = 1; }' \
	  '  if (o[8] != 8 || o[9] != 0 || o[10] != 0 || o[11] != 0) { puts("attrslen != 8"); bad = 1; }' \
	  '  if (o[12] != 0) { puts("layout != 0"); bad = 1; }' \
	  '  if (o[13] != 0 || o[14] != 0 || o[15] != 0) { puts("padding != 0"); bad = 1; }' \
	  '  free(o);' \
	  '  if (bad) return 1;' \
	  '  puts("head: 16 bytes, bodylen@0 attrslen@8 layout@12 padding@13..15 = 0");' \
	  '  return 0;' \
	  '}' > $$tmp/probe.c; \
	cc $$tmp/probe.c -o $$tmp/probe -I"$(KVSPACE_SRC)/include" \
	  -L"$(LIB)" -lkvspace -Wl,-rpath,"$(CURDIR)/$(LIB)" || { rm -rf $$tmp; exit 1; }; \
	$$tmp/probe; rc=$$?; rm -rf $$tmp; \
	if [ $$rc -ne 0 ]; then echo "kvspace 库的 head 布局不是 16 字节——先跑 make kvspace-libs"; fi; \
	exit $$rc

layout:
	cargo build --release --manifest-path layout/Cargo.toml
	mkdir -p $(BIN) $(LIB)
	cp layout/target/release/kvlanglayout $(BIN)/
	cp layout/target/release/$(LAYOUT_LIB) $(LIB)/

json:
	cd extruntime_tutorial/go/json && CGO_LDFLAGS="-L$(CURDIR)/$(LIB) -lkvspace" go build -o ../../../bin/json-rwirext ./cmd/

oldhero:
	cd oldhero && go build -ldflags="-s -w" -o ../bin/kvlang-go ./cmd/kvlang/

clean:
	rm -rf $(BIN) $(LIB) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path byteseek/Cargo.toml

# ── 安装态清理（install 的逆操作，kvspace / kvlang 生态） ─────────────────
#   make clear         删 .so（/usr/lib、/usr/lib/kvspace、/lib、/usr/local/lib …）+ 可执行
#   make clear-headers 再删头（/usr/include/kvspace、/usr/include/kvlang …）
#   make clear-all     clear + clear-headers
# 注：/lib、/bin 多为指向 /usr 的符号链接，重复删同一文件无副作用。
SUDO    ?= sudo
LIBDIRS := /usr/lib/kvspace /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib /usr/local/lib
BINDIRS := /usr/bin /bin /usr/local/bin
BINS    := byteseek kvlang kvlanglayout kvspace
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

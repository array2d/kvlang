# kvlang 统一构建：各自包含在 repo 内（可执行 bin/，库 lib/，后端 lib/kvspace/），
# 不装 /usr；各组件独立编译。
#   make runtime    旧 C runtime（已归档 oldhero/oldgo/runtime，非默认）
#   make runtimec2  旧 c2 runtime（已归档 oldhero/oldgo/runtimec2，非默认）
#   make byteseek   功能完整 Rust runtime → 编译为 byteseek（term/json/http/kvlanglayout/networld 就地 rwir）
#   make layout     Rust layout（kvlanglayout）
#   make oldhero    Go 旧 runtime（kvlang-go，兼容保留，即将归档）
#   make test       全量 tutorial 回归
#   make json       Go json 扩展（json-rwirext；待 TASK-042 升级，当前构建不出）
#   make clean      清理 bin/ 与各构建目录
#   make clear      install 的逆操作：删系统目录里 kvspace/kvlang 的 .so 与可执行
#   make status     列出系统目录里已安装的库 / 可执行 / 头
# kvspace 后端由 libkvspace dispatch 前端按 DSN 运行时选择，不再编译期切换。

# runtime / runtimec2 已归档到 oldhero/oldgo/（TASK-041），不再参与默认构建：
# 它们的源码冻结在旧 kvspace ABI 上，构建出来的是链接旧符号的库。
# 归档目录的产物落点是 oldhero/oldgo/lib，**不再落 kvlang/lib**，别误用。
# 两级布局（repo 即 prefix）：可执行 → bin/，库 → lib/（后端在 lib/kvspace/）。
BIN         := bin
LIB         := lib
BACKEND     := lib/kvspace

# 产物后缀：Linux=.so macOS=.dylib（uname 判 Darwin，跨平台确定）。
# C2_LIB 已随 runtimec2 归档一并删除：它只用于取 c2 产物名，
# 现在 c2 不进默认构建，留着就是一条指向已归档物的死引用。
ifeq ($(shell uname -s),Darwin)
LAYOUT_LIB := libkvlanglayout.dylib
else
LAYOUT_LIB := libkvlanglayout.so
endif

.PHONY: all runtime runtimec2 byteseek layout json oldhero test clean \
        status clear clear-headers clear-all single \
        kvspace-libs check-kvspace-head

# all 只留已迁移干净的组件。两个被摘的组件各有明确原因，不是临时回避：
#
#  · byteseek：boot 阶段经 libkvlang_runtimec2 调 kvlangDefRwir，而 c2 已归档、
#    runtimec 尚无该扩展注册 ABI（TASK-035）。
#
#  · json（extruntime_tutorial/go/json）：**不归档**，它是扩展 runtime 实现，
#    只是升级优先级靠后（排在 TASK-042 之后）。当前源码仍调 13 处旧 kvspace
#    符号且 cgo 链 -lkvlang_runtimec2，所以此刻构建不出来——等 TASK-042/035
#    落地后升级。目标保留在下面，不在 all 里。
#
# 两者加回 all 的条件见对应位置。
all: layout

test: all
	python3 tutorial/test.py --no-build

# 单一二进制（#493）：把 .so 作为字节嵌进 byteseek，启动时 memfd+dlopen，前端用
# RTLD_NOLOAD 复用已加载的后端。payload 固定组装到 build/embed（不读构建 env）。
# FRONTEND_SO 是构建期参数（不是运行时环境变量）：本分支构建的 libkvspace.so.1。
FRONTEND_SO ?=
# 单一二进制（#493）：**已停用**。它 embed 的是 runtimec2，而 c2 已归档、
# runtimec 尚无 kvlangDefRwir 等价 ABI（TASK-035）——现在构建出来的是启动即崩的
# 二进制。等 TASK-035 落地、embed 源改成 runtimec 后再恢复。
single:
	@echo "single 已停用：等待 TASK-035（runtimec 补齐 kvlangDefRwir 等价 ABI）" >&2
	@echo "原因：embed 源 runtimec2 已归档，boot 阶段扩展注册无对应物。" >&2
	@exit 1

# 归档靶：源码在 oldhero/oldgo/，产物落 oldhero/oldgo/lib（不进 kvlang/lib）。
# 仅用于归档构建/取证，**不是**日常构建路径。
runtime:
	cmake -S oldhero/oldgo/runtime -B build/runtime -DCMAKE_BUILD_TYPE=Release
	cmake --build build/runtime --target kvlang_runtime -j

# runtimec 的两个命令行入口，**并存且分工不同**：
#   kv_runner  —— 吃语料目录，批量编/跑（tools/ 下的诊断与回归用）
#   kvlangrun  —— 单文件 `create <src>` / `run <vid>` 两阶段驱动，
#                 benchmark/run.py 默认调的就是它（`--kvlang-bin` 可覆盖）
# 两个都要拷进 bin/：只拷 kv_runner 会让 benchmark 捡到 bin/ 里的陈旧二进制。
runtimec_runner:
	@$(call kvspace_require,KVSPACE_SRC,runtimec_runner,kvspace)
	cmake -S runtimec -B build/runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DKVSPACE_INCLUDE_DIRECTORY="$(KVSPACE_INCLUDE_DIRECTORY)" \
	  -DRUNTIMEC_KVSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)"
	cmake --build build/runtimec --target kv_runner kvlangrun -j
	mkdir -p $(BIN)
	cp build/runtimec/kv_runner $(BIN)/
	cp build/runtimec/kvlangrun $(BIN)/

runtimec2:
	cmake -S oldhero/oldgo/runtimec2 -B build/runtimec2 -DCMAKE_BUILD_TYPE=Release
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
# 两个源仓库**必填，没有默认值**：兄弟目录布局是开发者本地的偶然状态，不是契约。
# 它已经制造过三类事故——CMake 静默命中兄弟仓里非 pin 的版本、CI 把仓库 clone 到
# 相邻目录之后就再也不敢动目录结构、pod 重建后路径全失效而报错不指向真因。
# 默认值一旦存在，正确路径就变成「没被默认值遮住的那条」，而没有人会去验它。
#
#   make kvspace-libs KVSPACE_SRC=/path/to/kvspace KVSPACE_DUR_SRC=/path/to/kvspace-durable
#
# KVSPACE_INCLUDE_DIRECTORY 派生于**显式传入的** KVSPACE_SRC（同一个仓，不是猜的
# 兄弟路径）；想指向别处的头（比如已安装的 /usr/include）就显式覆盖它。
KVSPACE_INCLUDE_DIRECTORY ?= $(if $(strip $(KVSPACE_SRC)),$(KVSPACE_SRC)/include,)

# 变量在**引用时**检查：只用 kvlang-xvalue / check-kvspace-head 这类
# 不碰兄弟仓的目标时不必提供。
kvspace_require = $(if $(strip $($(1))),,$(error 缺少 $(1)：兄弟仓库不是默认值，请显式传入。例：make $(2) $(1)=/path/to/$(3)))

kvspace-libs: kvspace-frontend kvlang-xvalue kvspace-durable-lib
	$(MAKE) layout byteseek
	$(MAKE) kvspace-cli
	$(MAKE) check-kvspace-head

kvspace-frontend:
	@$(call kvspace_require,KVSPACE_SRC,kvspace-frontend,kvspace)
	@test -f "$(LIB)/libkvlang_xvalue.a" || { \
	  echo "缺少 $(LIB)/libkvlang_xvalue.a：先跑 make kvlang-xvalue"; exit 1; }
	# kvspace 的 CLI 造值要用语言层构造器，故配置期就要知道 .a 在哪（TASK-082）。
	cmake -S "$(KVSPACE_SRC)" -B build/kvspace -DCMAKE_BUILD_TYPE=Release \
	  -DKVLANG_XVALUE_LIBRARY_PATH="$(CURDIR)/$(LIB)/libkvlang_xvalue.a"
	cmake --build build/kvspace -j
	mkdir -p $(LIB)
	cp -P build/kvspace/libkvspace.so* $(LIB)/

# 语言层静态库（kvlangXvalueHeadNew* / kvlangXvalueNew* / kvlangCoordCmp /
# kvlangConstLanguage）：TASK-082 从 kvspace 搬来，layout / runtimec / byteseek / CLI
# 都链它。**先于** kvspace 的后端与 CLI 构建——CLI 造值要用这里的构造器。
#
# 这一条故意**不**依赖兄弟仓：kvspace 的头来自 $(LIB) 旁边的 kvspace 构建产物，
# 用参数指向它而不是「推一条相对路径」。
kvlang-xvalue:
	@$(call kvspace_require,KVSPACE_SRC,kvlang-xvalue,kvspace)
	cmake -S xvalue -B build/kvlang-xvalue -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_TESTING=OFF \
	  -DKVLANG_KVSPACE_INCLUDE_DIRECTORY="$(KVSPACE_INCLUDE_DIRECTORY)" \
	  -DKVLANG_KVSPACE_LIB_DIRECTORY="$(CURDIR)/$(LIB)"
	cmake --build build/kvlang-xvalue -j
	mkdir -p $(LIB)
	cp build/kvlang-xvalue/libkvlang_xvalue.a $(LIB)/

# kvspace-c 已冻结：shm 引擎并入 kvspace 仓，随 kvspace-frontend 一起产出 libkvspace。
# 该仓现在只有一条 FATAL_ERROR 桩，保留 target 等于留一条必断的依赖边。

kvspace-durable-lib:
	@$(call kvspace_require,KVSPACE_DUR_SRC,kvspace-durable-lib,kvspace-durable)
	cargo build --release --manifest-path "$(KVSPACE_DUR_SRC)/Cargo.toml"
	mkdir -p $(BACKEND)
	cp -P "$(KVSPACE_DUR_SRC)/target/release/libkvspace_durable.so" $(BACKEND)/libkvspace_durable.so.1

# kvspace CLI 由前端仓的 CMake 目标产出（cli/ 独立 Cargo 项目），装到 bin/。
kvspace-cli: kvspace-frontend
	@$(call kvspace_require,KVSPACE_SRC,kvspace-cli,kvspace)
	mkdir -p $(BIN)
	cp "$(KVSPACE_SRC)/cli/target/release/kvspace" $(BIN)/kvspace

# 探针：从**已装**的两个库产一个 XValue，检查 head 是 16 字节、字段落在正确偏移。
# 装错版本时在构建期就报错，而不是等到测试期看到一片 invalid XValue。
#
# 两个库缺一不可：构造器（kvlangXvalueHeadNewScalar）在 kvlang 的 libkvlang_xvalue.a
# （TASK-082 从 kvspace 搬出），head 布局与字节访问器在 libkvspace。只链其中一个，
# 探到的就不是「两者对同一份格式的理解」——那正是要防的那种错误。
check-kvspace-head:
	@$(call kvspace_require,KVSPACE_SRC,check-kvspace-head,kvspace)
	@test -f "$(LIB)/libkvlang_xvalue.a" || { \
	  echo "缺少 $(LIB)/libkvlang_xvalue.a：先构建 kvlang/xvalue"; exit 1; }
	@tmp=$$(mktemp -d); \
	printf '%s\n' \
	  '#include <kvspace/kvspace_xvalue.h>' \
	  '#include <kvlang/xvalue.h>' \
	  '#include <stdio.h>' \
	  '#include <stdlib.h>' \
	  'int main(void) {' \
	  '  uint8_t *o = NULL; uint64_t n = 0;' \
	  '  if (kvlangXvalueHeadNewScalar(' \
	  '        (const uint8_t *)"\x2a\0\0\0\0\0\0\0", 8, "int64",' \
	  '        &o, &n) != 0) { puts("ctor failed"); return 1; }' \
	  '  int bad = 0;' \
	  '  if (KVSPACE_XVALUE_HEAD_LEN != 16) { puts("HEAD_LEN != 16"); bad = 1; }' \
	  '  if (n != 16 + 8 + 1 + 2 + 5) { printf("total=%llu, want 32\\n", (unsigned long long)n); bad = 1; }' \
	  '  if (o[0] != 8 || o[1] != 0) { puts("bodylen != 8"); bad = 1; }' \
	  '  if (o[8] != 8 || o[9] != 0 || o[10] != 0 || o[11] != 0) { puts("attrslen != 8"); bad = 1; }' \
	  '  if (o[12] != 0) { puts("padding@12 != 0"); bad = 1; }' \
	  '  if (o[13] != 0 || o[14] != 0 || o[15] != 0) { puts("padding != 0"); bad = 1; }' \
	  '  free(o);' \
	  '  if (bad) return 1;' \
	  '  puts("head: 16 bytes, bodylen@0 attrslen@8 padding@10..15 = 0");' \
	  '  return 0;' \
	  '}' > $$tmp/probe.c; \
	cc $$tmp/probe.c -o $$tmp/probe -I"$(KVSPACE_SRC)/include" -I"$(CURDIR)/include" \
	  -L"$(LIB)" -lkvspace "$(LIB)/libkvlang_xvalue.a" \
	  -Wl,-rpath,"$(CURDIR)/$(LIB)" || { rm -rf $$tmp; exit 1; }; \
	$$tmp/probe; rc=$$?; rm -rf $$tmp; \
	if [ $$rc -ne 0 ]; then echo "kvspace 库的 head 布局不是 16 字节——先跑 make kvspace-libs"; fi; \
	exit $$rc

layout:
	cargo build --release --manifest-path layout/Cargo.toml
	mkdir -p $(BIN) $(LIB)
	cp layout/target/release/kvlanglayout $(BIN)/
	cp layout/target/release/$(LAYOUT_LIB) $(LIB)/

# json 扩展（extruntime_tutorial/go/json）→ bin/json-rwirext。
#
# **不归档**：它是扩展 runtime 实现，只是升级优先级靠后。**当前不在 all 里**
# （见上面 all 的注释），需要时显式 `make json`。
#
# **当前预期失败**：源码仍调 13 处旧 kvspace 符号（WriteInPlace/WriteNewPlace/
# Get/SetValue/ListLen/ListAt/Del/DelTree/Mkindex/NewChar），且 cgo LDFLAGS 链
# -lkvlang_runtimec2——该 .so 已随 c2 归档从 bin/ 删除，不会伪造恢复。
# 等 TASK-042（runtimec 补齐扩展 ABI）后升级源码并重建。
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

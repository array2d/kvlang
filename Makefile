# kvlang 统一构建：各自包含在 repo 内（可执行 bin/，库 lib/，后端 lib/kvspace/），
# 不装 /usr；各组件独立编译。
#   make byteseek   功能完整 Rust runtime → 编译为 byteseek（term/json/http/kvlanglayout/networld 就地 rwir）
#   make layout     Rust layout（kvlanglayout）
#   make test       全量 tutorial 回归
#   make json       Go json 扩展（json-rwirext；待 TASK-042 升级，当前构建不出）
#   make clean      清理 bin/ 与各构建目录
#   make clear      install 的逆操作：删系统目录里 kvspace/kvlang 的 .so 与可执行
#   make status     列出系统目录里已安装的库 / 可执行 / 头
# kvspace 后端由 libkvspace dispatch 前端按 DSN 运行时选择，不再编译期切换。

# 两级布局（repo 即 prefix）：可执行 → bin/，库 → lib/（后端在 lib/kvspace/）。
BIN         := bin
LIB         := lib
BACKEND     := lib/kvspace
# #493 embed payload 的落点。byteseek/build.rs 固定从这里读（include_bytes! 的路径
# 在编译期定死），所以位置不是自由选择——改这里必须同步改 build.rs。
EMBED_DIR   := build/embed

# 产物后缀：Linux=.so macOS=.dylib（uname 判 Darwin，跨平台确定）。
# C2_LIB 已随 runtimec2 归档一并删除：它只用于取 c2 产物名，
# 现在 c2 不进默认构建，留着就是一条指向已归档物的死引用。
ifeq ($(shell uname -s),Darwin)
LAYOUT_LIB := libkvlanglayout.dylib
else
LAYOUT_LIB := libkvlanglayout.so
endif

.PHONY: all artifact artifact-check byteseek layout json test clean \
        status clear clear-headers clear-all single \
        kvspace-libs check-kvspace-head stale-gate

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
all: stale-gate layout

# ── 论文 artifact 一键构建 ───────────────────────────────────────────
# 从 deps/ 源码编出全部运行产物，再跑 benchmark 验证 10 个 case 全部 valid。
# 零参数：所有路径由本文件从 $(CURDIR) 推出，不读环境变量、不要求兄弟仓。
#
# 产物：
#   lib/libkvspace.so*        ← deps/kvspace
#   lib/libkvlanglayout.so    ← layout/ (Rust)
#   lib/libkvlang_runtime.so  ← runtimec/
#   bin/kvlangrun, bin/kv_runner, bin/kvlanglayout
#
# lib/libkvlang_xvalue.a 树内已带（xvalue/ 构建入口已移除，源码在 src/）。
artifact: stale-gate
	@echo "== artifact: 构建依赖库（deps/） =="
	cmake -S "$(ARTIFACT_KVSPACE_SRC)" -B build/artifact-kvspace \
	  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	cmake --build build/artifact-kvspace -j
	mkdir -p $(LIB)
	cp -P build/artifact-kvspace/libkvspace.* $(LIB)/
	@echo "== artifact: 构建 layout =="
	cargo build --release --manifest-path layout/Cargo.toml
	mkdir -p $(BIN)
	cp layout/target/release/kvlanglayout $(BIN)/
	cp layout/target/release/$(LAYOUT_LIB) $(LIB)/
	@echo "== artifact: 构建 runtimec =="
	cmake -S runtimec -B build/artifact-runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DKVSPACE_INCLUDE_DIRECTORY="$(ARTIFACT_KVSPACE_SRC)/include" \
	  -DRUNTIMEC_KVSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)" \
	  -DKVLANG_XVALUE_LIBRARY_PATH="$(CURDIR)/$(LIB)/libkvlang_xvalue.a"
	cmake --build build/artifact-runtimec -j
	cp build/artifact-runtimec/kv_runner $(BIN)/
	cp build/artifact-runtimec/kvlangrun $(BIN)/
	cp -P build/artifact-runtimec/libkvlang_runtime.* $(LIB)/ 2>/dev/null || true
	@echo "== artifact: 构建完成 =="
	@ls -1 $(BIN) $(LIB)

# 构建 + 跑 benchmark（shm），10 个 case × 各规模，全部 valid 才算通过。
artifact-check: artifact
	python3 benchmark/run.py --backends shm --repeat 3

test: all
	python3 tutorial/test.py --no-build

# 单一二进制（#493）：把 .so 作为字节嵌进 byteseek，启动时 memfd+dlopen，前端用
# RTLD_NOLOAD 复用已加载的后端。payload 固定组装到 build/embed（不读构建 env）。
# FRONTEND_SO 是构建期参数（不是运行时环境变量）：本分支构建的 libkvspace.so.1。
FRONTEND_SO ?=
# 单一二进制（#493）：**已停用**。它 embed 的是 runtimec2，而 c2 已归档、
# runtimec 尚无 kvlangDefRwir 等价 ABI（TASK-035）——现在构建出来的是启动即崩的
# 二进制。等 TASK-035 落地、embed 源改成 runtimec 后再恢复。
# #493 单一二进制：把三个 .so 的字节嵌进 byteseek（memfd + dlopen，不落盘不 exec）。
#
# **不再是桩。** 从前这里是 `@echo "single 已停用" && exit 1`——桩把「TASK-035 没做」
# 和「这条链写错了」混成同一句话，两种情况的下一步完全不同。
#
# **末尾必须有运行期冒烟**：这一段是今天实测出来的，不是保险起见。
#
# embed 模式把链接期错误**推迟到运行期**：`build.rs` 为每个符号生成函数指针 + 运行期
# `dlsym`，于是链接器看不到任何未定义符号，`cargo build --features embed` **一定成功**。
# 实测：构建 rc=0，`bin/byteseek --help` 打印 `missing symbol kvlangXvalueHeadNewNone`
# 后退出。非 embed 模式则相反——链接期就红，一眼看得出缺哪 12 个。
#
# 所以判据是：**「构建成功」不等于「产物能用」**。别的 target 不必跑自己的产物（它们是
# 库/中间件），但 `single` 这个 target 的定义就是「产出一个能跑的单一二进制」——跑一次
# 是它自己的验收，不是外部耦合。
# **先验收、后安装。** 冒烟验的是 cargo 的产物（`byteseek/target/release/`），验过才
# `cp` 进 `bin/`。反过来做——先进 `bin/` 再验——失败时会留下一个「看起来有、跑起来崩」
# 的文件，要靠下一道 `bin-gate` 才发现。制造一个必须由下一道闸清理的垃圾，不如根本
# 不制造：失败时 `bin/` 保持上一次的干净状态。
single: embed-payload
	cargo build --release --features embed --manifest-path byteseek/Cargo.toml
	@out=$$(byteseek/target/release/byteseek --help 2>&1); rc=$$?; \
	if [ $$rc -ne 0 ]; then \
	  echo "single: 构建成功但产物跑不起来（rc=$$rc）——embed 把链接期错误推迟到了运行期。"; \
	  echo "  首行：$$(printf '%s\n' "$$out" | head -1)"; \
	  echo "  未安装到 $(BIN)/：先验收、后安装。"; \
	  exit 1; \
	fi
	mkdir -p $(BIN)
	cp byteseek/target/release/byteseek $(BIN)/byteseek

# embed payload：把要嵌进二进制的三个 .so 摆到 build/embed/（build.rs 固定从那里读，
# 不读任何构建环境变量）。
#
# 布局是**单库化之后的真实形态**，不是旧的六件套：
#   libkvspace.so.1        本仓单库（shm 引擎直接编在里面），不是 dispatch 前端
#   libkvlang_runtime.so   runtimec 产物
#   libkvlanglayout.so     layout 产物
#
# 旧布局里的 `kvspace/libkvspace-c.so*`（shm 后端）与 `libkvspace_durable.so*`
# 都已不存在——单库化后 `kvspaceConnect` 不 dlopen 任何后端，那两个文件是死产物。
# runtimec2 更是早已归档（TASK-041）。所以这里**没有子目录、没有第四第五个文件**。
#
# 三个都从 $(LIB) 取，不重新构建：构建是各自 target 的事（layout / runtimec-lib /
# kvspace-frontend）。本 target 只做「摆位置 + 过闸」。
embed-payload: stale-gate
	@for f in libkvspace.so.1 libkvlang_runtime.so libkvlanglayout.so; do \
	  if [ ! -f "$(LIB)/$$f" ]; then \
	    case "$$f" in \
	      libkvspace.so.1)       hint="kvspace-frontend";; \
	      libkvlang_runtime.so)  hint="runtimec-lib";; \
	      libkvlanglayout.so)    hint="layout";; \
	    esac; \
	    echo "embed-payload: 缺少 $(LIB)/$$f"; \
	    echo "  先跑：make $$hint"; \
	    exit 1; \
	  fi; \
	done
	rm -rf $(EMBED_DIR)
	mkdir -p $(EMBED_DIR)
	# **必须是 `cp -L`（解引用），不能是 `cp -P`。** `lib/libkvspace.so.1` 在本仓是
	# 指向 `libkvspace.so.1.0.0` 的符号链接；`-P` 把它原样拷成链接，而 payload 里没有
	# 被指向的实体——得到一个**断链**。而 build.rs 用 `include_bytes!` 读的是字节，
	# 链接对它毫无意义。断了不会在 embed-payload 这一步报错，要等编译期才炸。
	cp -L $(LIB)/libkvspace.so.1 $(LIB)/libkvlang_runtime.so $(LIB)/libkvlanglayout.so $(EMBED_DIR)/
	@for f in $(EMBED_DIR)/libkvspace.so.1 $(EMBED_DIR)/libkvlang_runtime.so $(EMBED_DIR)/libkvlanglayout.so; do \
	  if [ ! -f "$$f" ] || [ -L "$$f" ]; then \
	    echo "embed-payload: $$f 不是普通文件（断链或仍是链接）"; exit 1; \
	  fi; \
	done
	$(foreach f,$(wildcard $(EMBED_DIR)/*.so*),$(call kvspace_stale_gate,$(f))$(STALE_GATE_NEWLINE))
	@echo "embed payload: $(EMBED_DIR)"
	@ls -l $(EMBED_DIR)

# runtimec 的两个命令行入口，**并存且分工不同**：
#   kv_runner  —— 吃语料目录，批量编/跑（tools/ 下的诊断与回归用）
#   kvlangrun  —— 单文件 `create <src>` / `run <vid>` 两阶段驱动，
#                 benchmark/run.py 默认调的就是它（`--kvlang-bin` 可覆盖）
# 两个都要拷进 bin/：只拷 kv_runner 会让 benchmark 捡到 bin/ 里的陈旧二进制。
runtimec_runner: stale-gate
	@$(call kvspace_require,KVSPACE_SRC,runtimec_runner,kvspace)
	cmake -S runtimec -B build/runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DKVSPACE_INCLUDE_DIRECTORY="$(KVSPACE_INCLUDE_DIRECTORY)" \
	  -DRUNTIMEC_KVSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)"
	cmake --build build/runtimec --target kv_runner kvlangrun -j
	mkdir -p $(BIN)
	cp build/runtimec/kv_runner $(BIN)/
	cp build/runtimec/kvlangrun $(BIN)/

# runtimec 的运行库 → lib/libkvlang_runtime.so。
#
# 为什么单独一个 target：`runtimec_runner` 只拷**可执行文件**，不拷 .so。于是
# `lib/libkvlang_runtime.so` 长期靠手工 `cp`，没人重建——TASK-082 把构造器迁到
# kvlang 之后，那份旧 .so 仍引用 `kvspaceXvalueHeadNew*`，直到 byteseek 链接才炸，
# 而报错指向 byteseek。手工产物没有 target 就没有闸，没有闸就会变成陈旧输入。
#
# 闸在入口（`stale-gate`），不在拷完之后：见闸定义处「挂在拷贝后的闸恒绿」。
# 依赖 kvlang-xvalue：runtimec 的 contract 层链 libkvlang_xvalue.a。
runtimec-lib: stale-gate kvlang-xvalue
	@$(call kvspace_require,KVSPACE_SRC,runtimec-lib,kvspace)
	@test -f "$(LIB)/libkvlang_xvalue.a" || { \
	  echo "缺少 $(LIB)/libkvlang_xvalue.a：先跑 make kvlang-xvalue"; exit 1; }
	cmake -S runtimec -B build/runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DKVSPACE_INCLUDE_DIRECTORY="$(KVSPACE_INCLUDE_DIRECTORY)" \
	  -DRUNTIMEC_KVSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)" \
	  -DKVLANG_XVALUE_LIBRARY_PATH="$(CURDIR)/$(LIB)/libkvlang_xvalue.a"
	cmake --build build/runtimec --target kvlang_runtimec_scaffold -j
	mkdir -p $(LIB)
	# 通配而不是 `.so`：macOS 的产物是 `libkvlang_runtime.dylib`，写死 `.so` 在
	# darwin 上必然 `No such file or directory`。`libkvlang_runtime.*` 两平台各自
	# 只匹配该库自己的产物，比再维护一个 Darwin 分支少一条路。
	cp build/runtimec/libkvlang_runtime.* $(LIB)/

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

# ── 论文 artifact：零参数一键构建 ────────────────────────────────────
# `make artifact` 不要求任何外部路径：依赖源码在 deps/（kvspace / slotsboxmalloc /
# blockmalloc），全部从源码编到 lib/ 与 bin/，最后跑 benchmark 验证。
#
# KVSPACE_SRC 默认取 deps/kvspace——这是 artifact 树的自包含形态，不是「猜兄弟路径」：
# deps/ 就在仓内，路径由本文件定义，与开发者本地布局无关。显式传 KVSPACE_SRC= 可覆盖。
ARTIFACT_KVSPACE_SRC := $(CURDIR)/deps/kvspace

# 变量在**引用时**检查：只用 kvlang-xvalue / check-kvspace-head 这类
# 不碰兄弟仓的目标时不必提供。
kvspace_require = $(if $(strip $($(1))),,$(error 缺少 $(1)：兄弟仓库不是默认值，请显式传入。例：make $(2) $(1)=/path/to/$(3)))

# ── 陈旧 .so 闸 ──────────────────────────────────────────────────────
# **「链接成功」不等于「链接的是当前的库」。**
#
# `bin/`、`lib/` 是持久目录，而 make 的重建依赖不覆盖它们：上一次构建留在 `lib/`
# 里的 .so 会被下一次构建当作输入。真实的坑（TASK-082 之后）：`libkvlang_runtime.so`
# 仍是旧产物，里面还引用已被迁走的 `kvspaceXvalueHeadNew*`；直到 byteseek 链接时才
# 报 `undefined reference`，而报错指向的是 byteseek——**症状位置不是原因位置**。
#
# 为什么链接器没早报：`--no-allow-shlib-undefined` 只在被引用的符号**完全不存在**时
# 出声；「符号存在但是旧名字」它检查不到，因为旧名字并不在它的观察范围内。
#
# 判据：lib/ 下的 .so **不得**对已迁走/已删除的 API 存在未定义引用。这些名字是
# 历史遗留的黑名单，只增不减——名字一旦迁走，任何库再引用它都是陈旧产物。
#
# **闸挂在消费侧（构建入口），不挂在拷贝之后。** 挂在拷贝后的闸恒绿：`cp` 每次都用
# 新构建覆盖目标，闸检查的一定是刚拷进去的新货——「测试通过是因为它测的东西必然
# 成立」。闸要抓的是**没有任何 target 负责的残留**（`libkvlang_runtimec_scaffold.so`
# 混进发布包就是这种），所以要在构建开始/打包之前扫整个目录。
STALE_UNDEFINED_PATTERN := kvspaceXvalueHeadNew|kvspaceNewPtr|kvspaceNewChar|kvspaceNewBool|kvspaceNewInt64|kvspaceNewFloat64|kvspaceCoordCmp|kvspaceCoordIsCoord|kvspaceParseCoord|kvspaceConst$$
kvspace_stale_gate = @test -f "$(1)" || { echo "闸：$(1) 不存在"; exit 1; }; \
	if nm -D --undefined-only "$(1)" 2>/dev/null | grep -Eq '$(STALE_UNDEFINED_PATTERN)'; then \
	  echo "闸：$(1) 引用了已迁走/已删除的 API："; \
	  nm -D --undefined-only "$(1)" | grep -E '$(STALE_UNDEFINED_PATTERN)' | sed 's/^/  /'; \
	  echo "  这是陈旧产物（上次构建的残留被当成了输入）。先删掉它再重跑：rm -f $(1)"; \
	  exit 1; \
	fi

# 对 lib/ 下**每一个** .so/.dylib 过闸。
#
# 单条 recipe 里的 `$(call ...)` 用 foreach 拼会拼成一整行（宏内是 `; \` 续行），
# 拼出来就是 shell 语法错——所以这里用 make 的换行变量分隔，每个文件一条命令。
# release.yml 在打包前调它：`find`/通配收进 lib/ 的残留在那里最贵（直接发给用户）。
define STALE_GATE_NEWLINE


endef

# ── bin/ 的残留闸 ────────────────────────────────────────────────────
# `bin/` 与 `lib/` 一样是持久目录，同样会被上一次构建的产物污染，而可执行文件没有
# 符号表可查（多半是 strip 过或静态链接），所以判据换成**「这个文件该由哪个 target
# 重建」**：白名单里没有的名字，就是没人负责的残留。
#
# 真实的坑（coding3 踩的）：`bin/kv_runner` 是 21:55 的旧二进制，按旧算子名找不到
# handler 直接走 FOREIGN，语料从 164 掉到 160——**差点被报成性能/功能回归**。
#
# 为什么不「每次全量重建」：那样会让人想绕开闸。闸只回答「这个文件是不是该有人
# 负责」，指到 target 为止，重建由人决定。
#
# 白名单格式：文件名 = 产出它的 target。加新产物时同步加一行——漏加的症状是闸红，
# 而那正是我们要的（白名单缺项会立刻暴露，不会静默放过）。
#
# **白名单只列「现在真能构建、且构建出来真能用」的产物。** 加一条进白名单等于承认
# 这个产物是当前的、可用的。
#
#   - `byteseek`：`make single` 现在**能编译成功**（embed 模式把符号解析推迟到运行期），
#     但一跑就 `missing symbol kvlangXvalueHeadNewNone`——`kvlangXvalueHeadNew*` 只在
#     静态库 `libkvlang_xvalue.a` 里，而 embed payload 只有 `.so`，运行期 dlsym 找不到。
#     所以它不在白名单：**「构建成功」不等于「能用」**，白名单收的是后者。
#   - `json-rwirext`：target 构建不出（042/035）。
#
# 等 035/042 落地、真的能跑起来，再把名字加回来——那时加才是对的。
BIN_OWNERS := \
	kv_runner=runtimec_runner \
	kvlangrun=runtimec_runner \
	kvspace=kvspace-cli \
	kvlanglayout=layout

# 两个判据，各一次集合运算，无循环、无 case、无标志位：
#   1. actual − keys 非空 → `bin/` 里有不属于任何 target 的文件（陈旧残留）
#   2. 名字在但不是普通文件 → 目录 / 断链（`ls` 列得出、`-f` 为假的那类）
#
# **没有 `keys − actual`（「某 target 未产出」）这一条**，虽然集合差写起来对称。
# 为什么：闸的职责是「`bin/` 里不能有没归属的陈旧产物」，不是「所有 target 都必须
# 已构建」——后者是构建图的事。
#   - release 只跑 layout / kvspace-cli / runtimec-lib，**从不跑 runtimec_runner**，
#     于是 `bin/` 里本就没有 kv_runner / kvlangrun；
#   - `stale-gate` → `bin-gate` 挂在**所有构建入口**上，干净树 `bin/` 为空时
#     `keys − actual` 等于全部四条，`make` 在起手第一步就红。
# 两条都不是特例，是「判据管错了对象」。删掉它是**减分支**，不是加兜底。
#
# 用临时文件而不是 `<(...)`：进程替换是 bash 扩展，make 默认走 `/bin/sh`。
# 为一条闸给整个 Makefile 设 `SHELL := /bin/bash` 是拿全局换局部。
bin-gate:
	@k=$$(mktemp); a=$$(mktemp); \
	printf '%s\n' $(BIN_OWNERS) | cut -d= -f1 | sort > "$$k"; \
	ls -1 $(BIN) 2>/dev/null | sort > "$$a"; \
	trap 'rm -f "$$k" "$$a"' EXIT; \
	unexpected=$$(comm -13 "$$k" "$$a"); \
	notfile=$$(find $(BIN) -mindepth 1 -maxdepth 1 ! -type f 2>/dev/null | xargs -r -n1 basename | sort); \
	[ -z "$$unexpected" ] || { echo "闸：$(BIN)/ 里有不属于任何 target 的文件："; printf '%s\n' "$$unexpected"; exit 1; }; \
	[ -z "$$notfile" ]    || { echo "闸：以下名字在但不是普通文件（目录 / 断链）："; printf '%s\n' "$$notfile"; exit 1; }
	@echo "闸：$(BIN)/ 下全部可执行有归属"

stale-gate: bin-gate
	@$(foreach f,$(wildcard $(LIB)/*.so* $(LIB)/*.dylib),$(call kvspace_stale_gate,$(f))$(STALE_GATE_NEWLINE))
	@echo "闸：$(LIB)/ 下全部库通过"

kvspace-libs: kvspace-frontend kvlang-xvalue kvspace-durable-lib
	$(MAKE) layout byteseek
	$(MAKE) kvspace-cli
	$(MAKE) check-kvspace-head

# kvspace 的构建**不需要** libkvlang_xvalue.a：085 把语言层命令迁走后，kvspace CLI
# 只做存储子命令、零引用 kvlang 符号。原先那条「CLI 造值要用语言层构造器」是迁移前
# 的遗留，它顺带造成了一个循环依赖（kvspace-frontend 要 .a → .a 要 libkvspace），
# 从干净树构建必死在起手第一环。既不必要又致故障，删掉。
# kvspace 的两个 header-only 依赖（blockmalloc / slotsboxmalloc）由 `ci/deps.sh`
# 装到平台最终位置（Linux `/usr/include`、macOS `/usr/local/include`），kvspace 的
# `find_path` 候选就覆盖这两处——所以这里不再传任何 `-D`。
#
# 曾经这里有个条件变量：repo `include/` 有头就传 repo 路径、否则让 kvspace 走系统候选。
# 那是在给「repo 内的依赖副本」打补丁——副本本身是二义来源（runtimec 的 stale-copy
# 闸要求 `include/kvspace/` 不存在，别的消费者又要求它存在），补丁只能让两边轮流出红。
# 依赖统一装系统最终位置之后，副本消失，条件分支也就不需要了。
kvspace-frontend: stale-gate
	@$(call kvspace_require,KVSPACE_SRC,kvspace-frontend,kvspace)
	cmake -S "$(KVSPACE_SRC)" -B build/kvspace -DCMAKE_BUILD_TYPE=Release
	cmake --build build/kvspace -j
	mkdir -p $(LIB)
	# 通配而不是 `.so*`：macOS 的产物是 `libkvspace.dylib`，写死 `.so` 在 darwin 上
	# 必然 `No such file or directory`。`libkvspace.*` 两平台各自只匹配该库自己的
	# 产物（Linux 的 `.so` / `.so.1` / `.so.1.0.0`，macOS 的 `.dylib`）。
	cp -P build/kvspace/libkvspace.* $(LIB)/

# 语言层静态库（kvlangXvalueHeadNew* / kvlangXvalueNew* / kvlangCoordCmp /
# kvlangConstLanguage）：TASK-082 从 kvspace 搬来，layout / runtimec 都链它。
#
# **构建入口已移除**：论文 artifact 不带 xvalue/ 的 CMakeLists，该 .a 以预编译形态
# 随 lib/ 提供。源码仍在 src/（xvalue.c / codec.c / coord.c / const.c），需要重建时
# 按 xvalue/CMakeLists.txt 的原始定义自行加回构建入口。
kvlang-xvalue:
	@echo "kvlang-xvalue: 构建入口已从 artifact 移除；lib/libkvlang_xvalue.a 为预编译提供。"
	@echo "  源码：src/xvalue.c src/codec.c src/coord.c src/const.c"
	@test -f "$(LIB)/libkvlang_xvalue.a" || { \
	  echo "缺少 $(LIB)/libkvlang_xvalue.a，且本树不含构建入口。"; exit 1; }

# kvspace-c 已冻结：shm 引擎并入 kvspace 仓，随 kvspace-frontend 一起产出 libkvspace。
# 该仓现在只有一条 FATAL_ERROR 桩，保留 target 等于留一条必断的依赖边。

kvspace-durable-lib: stale-gate
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

# 依赖 kvlang-xvalue：layout 的 build.rs 链 libkvlang_xvalue.a。
layout: stale-gate kvlang-xvalue
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

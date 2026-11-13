# Unified pathlang build: self-contained inside the repo (binaries in bin/,
# libraries in lib/, backends in lib/pathspace/), nothing installed to /usr.
#   make pathseek   Full Rust runtime -> bin/pathseek (in-process rwir: term/json/http/pathlanglayout/networld)
#   make layout     Rust layout compiler (pathlanglayout)
#   make test       Full tutorial regression suite
#   make json       Go json extension (json-rwirext; pending TASK-042 upgrade)
#   make clean      Remove bin/, lib/, and build directories
#   make clear      Undo system install: remove pathspace/pathlang .so and binaries from system paths
#   make status     List libraries / binaries / headers installed in system paths
# pathspace backends are selected at runtime by DSN via the libpathspace dispatch frontend.

# Two-level layout (repo as prefix): binaries -> bin/, libraries -> lib/ (backends in lib/pathspace/).
BIN         := bin
LIB         := lib
BACKEND     := lib/pathspace
# Staging directory for #493 embedded payload. pathseek/build.rs reads from this
# fixed path via include_bytes! at compile time; keep in sync with build.rs.
EMBED_DIR   := build/embed

# Shared library suffix: Linux=.so, macOS=.dylib.
# C2_LIB was removed when runtimec2 was archived.
ifeq ($(shell uname -s),Darwin)
LAYOUT_LIB := libpathlanglayout.dylib
else
LAYOUT_LIB := libpathlanglayout.so
endif

.PHONY: all artifact artifact-check pathseek layout json test clean \
        status clear clear-headers clear-all single \
        pathspace-libs check-pathspace-head stale-gate

# `all` includes only cleanly migrated components. Excluded targets:
#
#  · pathseek: calls pathlangDefRwir via archived libpathlang_runtimec2 at boot;
#    runtimec does not yet expose that extension registration ABI (TASK-035).
#
#  · json (extruntime_tutorial/go/json): external runtime implementation, kept
#    unarchived pending TASK-042/035 upgrade (still references legacy pathspace
#    symbols and -lpathlang_runtimec2).
#
# Both targets remain available explicitly below.
all: stale-gate layout

# -- Paper artifact one-step build -------------------------------------------
# Builds all runtime artifacts from vendored sources in deps/.
# Zero arguments: all paths derive from $(CURDIR), independent of env or sibling repos.
#
# Outputs:
#   lib/libpathspace.so*        <- deps/pathspace
#   lib/libpathlanglayout.so    <- layout/ (Rust)
#   lib/libpathlang_runtime.so  <- runtimec/
#   bin/pathlangrun, bin/kv_runner, bin/pathlanglayout
#
# lib/libpathlang_xvalue.a is built from xvalue/.
artifact: stale-gate
	@echo "== artifact: building dependency libraries (deps/) =="
	cmake -S "$(ARTIFACT_PATHSPACE_SRC)" -B build/artifact-pathspace \
	  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	cmake --build build/artifact-pathspace -j
	mkdir -p $(LIB)
	cp -P build/artifact-pathspace/libpathspace.* $(LIB)/
	@echo "== artifact: building xvalue (language-layer static lib) =="
	cmake -S xvalue -B build/artifact-xvalue -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_TESTING=OFF \
	  -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY="$(ARTIFACT_PATHSPACE_SRC)/include" \
	  -DPATHLANG_PATHSPACE_LIB_DIRECTORY="$(CURDIR)/$(LIB)"
	cmake --build build/artifact-xvalue -j
	cp build/artifact-xvalue/libpathlang_xvalue.a $(LIB)/
	@echo "== artifact: building layout =="
	cargo build --release --manifest-path layout/Cargo.toml
	mkdir -p $(BIN)
	cp layout/target/release/pathlanglayout $(BIN)/
	cp layout/target/release/$(LAYOUT_LIB) $(LIB)/
	@echo "== artifact: building runtimec =="
	cmake -S runtimec -B build/artifact-runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DPATHSPACE_INCLUDE_DIRECTORY="$(ARTIFACT_PATHSPACE_SRC)/include" \
	  -DRUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)" \
	  -DPATHLANG_XVALUE_LIBRARY_PATH="$(CURDIR)/$(LIB)/libpathlang_xvalue.a"
	cmake --build build/artifact-runtimec -j
	cp build/artifact-runtimec/kv_runner $(BIN)/
	cp build/artifact-runtimec/pathlangrun $(BIN)/
	cp -P build/artifact-runtimec/libpathlang_runtime.* $(LIB)/ 2>/dev/null || true
	@echo "== artifact: build complete =="
	@ls -1 $(BIN) $(LIB)

# Build artifacts + run shm benchmarks (10 cases × scales; all must be valid).
artifact-check: artifact
	python3 benchmark/run.py --backends shm --repeat 3

test: all
	python3 tutorial/test.py --no-build

# Single-binary build (#493): embeds three .so files as byte arrays into pathseek
# (loaded at startup via memfd + dlopen). Payload is staged in build/embed/.
FRONTEND_SO ?=
# Note: embed mode defers link errors to runtime because build.rs generates
# function pointers resolved via dlsym at startup. Therefore `cargo build --features embed`
# always succeeds at link time even if symbols are missing.
# Run a runtime smoke check (`--help`) on the built binary BEFORE copying into bin/
# so a broken binary never pollutes bin/.
single: embed-payload
	cargo build --release --features embed --manifest-path pathseek/Cargo.toml
	@out=$$(pathseek/target/release/pathseek --help 2>&1); rc=$$?; \
	if [ $$rc -ne 0 ]; then \
	  echo "single: build succeeded but binary failed smoke check (rc=$$rc) -- embed defers link errors to runtime."; \
	  echo "  first line: $$(printf '%s\n' "$$out" | head -1)"; \
	  echo "  not installed to $(BIN)/: verify before install."; \
	  exit 1; \
	fi
	mkdir -p $(BIN)
	cp pathseek/target/release/pathseek $(BIN)/pathseek

# Stage the three shared libraries into build/embed/ for pathseek/build.rs:
#   libpathspace.so.1        unified pathspace library (shm engine built in)
#   libpathlang_runtime.so   runtimec shared library
#   libpathlanglayout.so     layout shared library
#
# All three are copied from $(LIB) without rebuilding; build their respective
# targets (layout / runtimec-lib / pathspace-frontend) beforehand.
embed-payload: stale-gate
	@for f in libpathspace.so.1 libpathlang_runtime.so libpathlanglayout.so; do \
	  if [ ! -f "$(LIB)/$$f" ]; then \
	    case "$$f" in \
	      libpathspace.so.1)       hint="pathspace-frontend";; \
	      libpathlang_runtime.so)  hint="runtimec-lib";; \
	      libpathlanglayout.so)    hint="layout";; \
	    esac; \
	    echo "embed-payload: missing $(LIB)/$$f"; \
	    echo "  run first: make $$hint"; \
	    exit 1; \
	  fi; \
	done
	rm -rf $(EMBED_DIR)
	mkdir -p $(EMBED_DIR)
	# Must use `cp -L` (dereference), not `cp -P`: lib/libpathspace.so.1 is a symlink
	# to libpathspace.so.1.0.0, and build.rs reads raw file bytes via include_bytes!.
	cp -L $(LIB)/libpathspace.so.1 $(LIB)/libpathlang_runtime.so $(LIB)/libpathlanglayout.so $(EMBED_DIR)/
	@for f in $(EMBED_DIR)/libpathspace.so.1 $(EMBED_DIR)/libpathlang_runtime.so $(EMBED_DIR)/libpathlanglayout.so; do \
	  if [ ! -f "$$f" ] || [ -L "$$f" ]; then \
	    echo "embed-payload: $$f is not a regular file (broken or remaining symlink)"; exit 1; \
	  fi; \
	done
	$(foreach f,$(wildcard $(EMBED_DIR)/*.so*),$(call pathspace_stale_gate,$(f))$(STALE_GATE_NEWLINE))
	@echo "embed payload: $(EMBED_DIR)"
	@ls -l $(EMBED_DIR)

# Two CLI entrypoints for runtimec:
#   kv_runner   -- batch corpus compile/run driver (diagnostics & regression)
#   pathlangrun -- two-stage single-file driver (`create <src>` / `run <vid>`),
#                  used by benchmark/run.py by default (`--pathlang-bin` overrides)
# Copy both into bin/ so benchmarks never pick up a stale binary.
runtimec_runner: stale-gate
	@$(call pathspace_require,PATHSPACE_SRC,runtimec_runner,pathspace)
	cmake -S runtimec -B build/runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DPATHSPACE_INCLUDE_DIRECTORY="$(PATHSPACE_INCLUDE_DIRECTORY)" \
	  -DRUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)"
	cmake --build build/runtimec --target kv_runner pathlangrun -j
	mkdir -p $(BIN)
	cp build/runtimec/kv_runner $(BIN)/
	cp build/runtimec/pathlangrun $(BIN)/

# runtimec shared library -> lib/libpathlang_runtime.{so,dylib}.
# Separate from runtimec_runner (which only installs CLI executables) so the
# shared library has an explicit build target and entry stale-gate.
# Depends on pathlang-xvalue (linked by runtimec's contract layer).
runtimec-lib: stale-gate pathlang-xvalue
	@$(call pathspace_require,PATHSPACE_SRC,runtimec-lib,pathspace)
	@test -f "$(LIB)/libpathlang_xvalue.a" || { \
	  echo "missing $(LIB)/libpathlang_xvalue.a: run make pathlang-xvalue first"; exit 1; }
	cmake -S runtimec -B build/runtimec -DCMAKE_BUILD_TYPE=Release \
	  -DPATHSPACE_INCLUDE_DIRECTORY="$(PATHSPACE_INCLUDE_DIRECTORY)" \
	  -DRUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY="$(CURDIR)/$(LIB)" \
	  -DPATHLANG_XVALUE_LIBRARY_PATH="$(CURDIR)/$(LIB)/libpathlang_xvalue.a"
	cmake --build build/runtimec --target pathlang_runtimec_scaffold -j
	mkdir -p $(LIB)
	# Use wildcard instead of `.so` so both Linux (.so) and macOS (.dylib) match.
	cp build/runtimec/libpathlang_runtime.* $(LIB)/

# stdlib is a compile-time input to pathseek: build.rs embeds stdlib/**/*.kv via
# include_str!. Track stdlib sources in Make so edits outside pathseek/ trigger rebuilds.
PATHSEEK_STDLIB_SOURCES := $(shell find stdlib -name '*.kv')
PATHSEEK_BINARY := $(BIN)/pathseek

pathseek: $(PATHSEEK_BINARY)

$(PATHSEEK_BINARY): $(PATHSEEK_STDLIB_SOURCES) pathseek/Cargo.toml
	cargo build --release --manifest-path pathseek/Cargo.toml
	mkdir -p $(BIN)
	cp pathseek/target/release/pathseek $(BIN)/pathseek

# -- pathspace libraries + consumers -----------------------------------------
# The XValue head is a cross-process ABI; frontend, backends, and CLI binaries
# must all be built against the same layout, then verified with a 16-byte head probe.
#
#   make pathspace-libs         Build libraries + layout/pathseek/pathspace CLI + run probe
#   make check-pathspace-head   Run the 16-byte head probe only (read-only)
#
# External source repo paths have no implicit sibling defaults; pass them explicitly:
#   make pathspace-libs PATHSPACE_SRC=/path/to/pathspace PATHSPACE_DUR_SRC=/path/to/pathspace-durable
#
# PATHSPACE_INCLUDE_DIRECTORY derives from explicit PATHSPACE_SRC unless overridden.
PATHSPACE_INCLUDE_DIRECTORY ?= $(if $(strip $(PATHSPACE_SRC)),$(PATHSPACE_SRC)/include,)

# -- Paper artifact: self-contained pathspace source -------------------------
# `make artifact` uses vendored sources under deps/ with no external repo paths needed.
ARTIFACT_PATHSPACE_SRC := $(CURDIR)/deps/pathspace

# Checked lazily when referenced so targets not touching external repos need no args.
pathspace_require = $(if $(strip $($(1))),,$(error Missing $(1): pass explicitly, e.g. make $(2) $(1)=/path/to/$(3)))

# -- Stale shared-library gate -----------------------------------------------
# bin/ and lib/ persist across runs, so stale .so files from earlier builds can
# remain in lib/ and reference removed/migrated symbols.
# Gate runs at build entry (before copying new outputs) and rejects any library
# in lib/ that has undefined references to retired API symbols.
STALE_UNDEFINED_PATTERN := pathspaceXvalueHeadNew|pathspaceNewPtr|pathspaceNewChar|pathspaceNewBool|pathspaceNewInt64|pathspaceNewFloat64|pathspaceCoordCmp|pathspaceCoordIsCoord|pathspaceParseCoord|pathspaceConst$$
pathspace_stale_gate = @test -f "$(1)" || { echo "gate: $(1) does not exist"; exit 1; }; \
	if nm -D --undefined-only "$(1)" 2>/dev/null | grep -Eq '$(STALE_UNDEFINED_PATTERN)'; then \
	  echo "gate: $(1) references migrated/removed API symbols:"; \
	  nm -D --undefined-only "$(1)" | grep -E '$(STALE_UNDEFINED_PATTERN)' | sed 's/^/  /'; \
	  echo "  stale build artifact detected; remove it and retry: rm -f $(1)"; \
	  exit 1; \
	fi

# Separate foreach-expanded gate calls onto distinct recipe lines.
define STALE_GATE_NEWLINE


endef

# -- Stale bin/ gate ---------------------------------------------------------
# Every file present in bin/ must belong to an active, working build target listed
# in BIN_OWNERS and must be a regular file (not a directory or broken symlink).
# Missing targets are allowed (clean tree or partial builds); unowned files fail.
BIN_OWNERS := \
	kv_runner=runtimec_runner \
	pathlangrun=runtimec_runner \
	pathspace=pathspace-cli \
	pathlanglayout=layout

# Two set checks using temp files (POSIX /bin/sh compatible, no process substitution):
#   1. actual - keys != empty -> unowned residual file in bin/
#   2. non-regular entry in bin/ -> directory or broken symlink
bin-gate:
	@k=$$(mktemp); a=$$(mktemp); \
	printf '%s\n' $(BIN_OWNERS) | cut -d= -f1 | sort > "$$k"; \
	ls -1 $(BIN) 2>/dev/null | sort > "$$a"; \
	trap 'rm -f "$$k" "$$a"' EXIT; \
	unexpected=$$(comm -13 "$$k" "$$a"); \
	notfile=$$(find $(BIN) -mindepth 1 -maxdepth 1 ! -type f 2>/dev/null | xargs -r -n1 basename | sort); \
	[ -z "$$unexpected" ] || { echo "gate: unowned files in $(BIN)/:"; printf '%s\n' "$$unexpected"; exit 1; }; \
	[ -z "$$notfile" ]    || { echo "gate: non-regular entries in $(BIN)/ (dir / broken symlink):"; printf '%s\n' "$$notfile"; exit 1; }
	@echo "gate: all executables in $(BIN)/ have owners"

stale-gate: bin-gate
	@$(foreach f,$(wildcard $(LIB)/*.so* $(LIB)/*.dylib),$(call pathspace_stale_gate,$(f))$(STALE_GATE_NEWLINE))
	@echo "gate: all libraries in $(LIB)/ passed"

pathspace-libs: pathspace-frontend pathlang-xvalue pathspace-durable-lib
	$(MAKE) layout pathseek
	$(MAKE) pathspace-cli
	$(MAKE) check-pathspace-head

# pathspace frontend + built-in shm engine (does not depend on libpathlang_xvalue.a;
# header-only deps blockalloc / slotboxalloc are resolved from system include paths
# or vendored deps/).
pathspace-frontend: stale-gate
	@$(call pathspace_require,PATHSPACE_SRC,pathspace-frontend,pathspace)
	cmake -S "$(PATHSPACE_SRC)" -B build/pathspace -DCMAKE_BUILD_TYPE=Release
	cmake --build build/pathspace -j
	mkdir -p $(LIB)
	# Wildcard matches both Linux (.so / .so.1 / .so.1.0.0) and macOS (.dylib).
	cp -P build/pathspace/libpathspace.* $(LIB)/

# Language-layer static library (pathlangXvalueHeadNew* / pathlangXvalueNew* /
# pathlangCoordCmp / pathlangConstLanguage), linked by layout and runtimec.
pathlang-xvalue: pathspace-frontend
	@$(call pathspace_require,PATHSPACE_SRC,pathlang-xvalue,pathspace)
	cmake -S xvalue -B build/pathlang-xvalue -DCMAKE_BUILD_TYPE=Release \
	  -DBUILD_TESTING=OFF \
	  -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY="$(PATHSPACE_INCLUDE_DIRECTORY)" \
	  -DPATHLANG_PATHSPACE_LIB_DIRECTORY="$(CURDIR)/$(LIB)"
	cmake --build build/pathlang-xvalue -j
	mkdir -p $(LIB)
	cp build/pathlang-xvalue/libpathlang_xvalue.a $(LIB)/

# pathspace-c is archived: shm engine now lives inside pathspace (libpathspace).

pathspace-durable-lib: stale-gate
	@$(call pathspace_require,PATHSPACE_DUR_SRC,pathspace-durable-lib,pathspace-durable)
	cargo build --release --manifest-path "$(PATHSPACE_DUR_SRC)/Cargo.toml"
	mkdir -p $(BACKEND)
	cp -P "$(PATHSPACE_DUR_SRC)/target/release/libpathspace_durable.so" $(BACKEND)/libpathspace_durable.so.1

# pathspace CLI is built by the pathspace CMake target (cli/ Cargo project) and copied to bin/.
pathspace-cli: pathspace-frontend
	@$(call pathspace_require,PATHSPACE_SRC,pathspace-cli,pathspace)
	mkdir -p $(BIN)
	cp "$(PATHSPACE_SRC)/cli/target/release/pathspace" $(BIN)/pathspace

# Probe: construct an XValue linking both libpathlang_xvalue.a and libpathspace,
# and verify the 16-byte head layout and field offsets at build time.
check-pathspace-head:
	@$(call pathspace_require,PATHSPACE_SRC,check-pathspace-head,pathspace)
	@test -f "$(LIB)/libpathlang_xvalue.a" || { \
	  echo "missing $(LIB)/libpathlang_xvalue.a: build pathlang/xvalue first"; exit 1; }
	@tmp=$$(mktemp -d); \
	printf '%s\n' \
	  '#include <pathspace/pathspace_xvalue.h>' \
	  '#include <pathlang/xvalue.h>' \
	  '#include <stdio.h>' \
	  '#include <stdlib.h>' \
	  'int main(void) {' \
	  '  uint8_t *o = NULL; uint64_t n = 0;' \
	  '  if (pathlangXvalueHeadNewScalar(' \
	  '        (const uint8_t *)"\x2a\0\0\0\0\0\0\0", 8, "int64",' \
	  '        &o, &n) != 0) { puts("ctor failed"); return 1; }' \
	  '  int bad = 0;' \
	  '  if (PATHSPACE_XVALUE_HEAD_LEN != 16) { puts("HEAD_LEN != 16"); bad = 1; }' \
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
	cc $$tmp/probe.c -o $$tmp/probe -I"$(PATHSPACE_SRC)/include" -I"$(CURDIR)/include" \
	  -L"$(LIB)" -lpathspace "$(LIB)/libpathlang_xvalue.a" \
	  -Wl,-rpath,"$(CURDIR)/$(LIB)" || { rm -rf $$tmp; exit 1; }; \
	$$tmp/probe; rc=$$?; rm -rf $$tmp; \
	if [ $$rc -ne 0 ]; then echo "pathspace head layout is not 16 bytes -- run make pathspace-libs first"; fi; \
	exit $$rc

# Depends on pathlang-xvalue: layout/build.rs links libpathlang_xvalue.a.
layout: stale-gate pathlang-xvalue
	cargo build --release --manifest-path layout/Cargo.toml
	mkdir -p $(BIN) $(LIB)
	cp layout/target/release/pathlanglayout $(BIN)/
	cp layout/target/release/$(LAYOUT_LIB) $(LIB)/

# Go json extension (extruntime_tutorial/go/json) -> bin/json-rwirext.
# Excluded from `all` pending TASK-042/035 upgrade from legacy pathspace/runtimec2 ABI.
json:
	cd extruntime_tutorial/go/json && CGO_LDFLAGS="-L$(CURDIR)/$(LIB) -lpathspace" go build -o ../../../bin/json-rwirext ./cmd/

clean:
	rm -rf $(BIN) $(LIB) build
	cargo clean --manifest-path layout/Cargo.toml
	cargo clean --manifest-path pathseek/Cargo.toml

# -- Uninstall system-wide artifacts (inverse of install.sh) -----------------
#   make clear         Remove .so files and binaries from system paths
#   make clear-headers Remove headers from system include paths
#   make clear-all     clear + clear-headers
# Note: /lib and /bin are often symlinks to /usr; repeated rm -f is a safe no-op.
SUDO    ?= sudo
LIBDIRS := /usr/lib/pathspace /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib /usr/local/lib
BINDIRS := /usr/bin /bin /usr/local/bin
BINS    := pathseek pathlang pathlanglayout pathspace
HDRDIRS := /usr/include/pathspace /usr/include/pathlang /usr/local/include/pathspace /usr/local/include/pathlang
PCDIRS  := /usr/lib/x86_64-linux-gnu/pkgconfig /usr/lib/pkgconfig /usr/local/lib/pkgconfig

status:
	@echo "-- Libraries --"
	@for d in $(LIBDIRS); do ls -1 $$d/libpathspace*.so* $$d/libpathlang*.so* 2>/dev/null | sed 's|^|  |'; done; true
	@for d in $(PCDIRS); do [ -e $$d/pathspace.pc ] && echo "  $$d/pathspace.pc"; done; true
	@echo "-- Executables --"
	@for d in $(BINDIRS); do for b in $(BINS); do [ -e $$d/$$b ] && echo "  $$d/$$b"; done; done; true
	@echo "-- Headers --"
	@for d in $(HDRDIRS); do [ -e $$d ] && echo "  $$d"; done; true

clear:
	@for d in $(LIBDIRS); do $(SUDO) rm -f $$d/libpathspace*.so* $$d/libpathlang*.so*; done
	@for d in $(PCDIRS); do $(SUDO) rm -f $$d/pathspace.pc; done
	@for d in $(BINDIRS); do for b in $(BINS); do $(SUDO) rm -f $$d/$$b; done; done
	@$(SUDO) ldconfig 2>/dev/null || true
	@echo "[ok] clear: removed pathspace/pathlang shared libraries and binaries"

clear-headers:
	@$(SUDO) rm -rf $(HDRDIRS)
	@echo "[ok] clear-headers: removed header directories"

clear-all: clear clear-headers

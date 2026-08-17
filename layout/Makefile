.PHONY: build test clean

# 前置：kvspace-durable 必须先产出 libkvspace_durable.so（layout 通过 C ABI 动态链接它）。
# 两段式：durable(../..) 先 build --release，再 build 本 crate。
build:
	cd ../../kvspace-durable && cargo build --release
	cargo build

test: build
	cargo test

clean:
	cargo clean

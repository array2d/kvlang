.PHONY: build test clean

# Prerequisite: pathspace-durable must build libpathspace_durable.so first (layout links it dynamically via the C ABI).
# Two-stage build: build durable (../..) in release mode first, then build this crate.
build:
	cd ../../pathspace-durable && cargo build --release
	cargo build

test: build
	cargo test

clean:
	cargo clean

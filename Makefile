.PHONY: build test vet clean kvspace install rust rust-test

export GOPROXY ?= https://goproxy.cn,direct
PREFIX   ?= ~/.local

build:
	go mod tidy
	go build -ldflags="-s -w" -o kvlang ./cmd/kvlang/
	install -d $(PREFIX)/bin
	install kvlang $(PREFIX)/bin/kvlang

rust:
	cargo build --manifest-path $(CURDIR)/Cargo.toml

rust-test:
	python3 tutorial/test.py --runtime=rust

vet:
	go vet ./...

clean:
	go clean
	cargo clean --manifest-path $(CURDIR)/Cargo.toml
	rm -f kvlang

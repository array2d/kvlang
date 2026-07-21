.PHONY: build test vet clean kvspace install

export GOPROXY ?= https://goproxy.cn,direct
PREFIX   ?= ~./local

build:
	go mod tidy
	go build -ldflags="-s -w" -o kvlang ./cmd/kvlang/

install: build
	install -d $(PREFIX)/bin
	install kvlang $(PREFIX)/bin/kvlang

vet:
	go vet ./...

clean:
	go clean
	rm -f kvlang

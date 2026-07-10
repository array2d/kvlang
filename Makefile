.PHONY: build test vet clean kvspace install

export GOPROXY ?= https://goproxy.cn,direct
PREFIX   ?= /usr/local

build:
	go mod tidy
	go build -ldflags="-s -w" -o kvlang ./cmd/kvlang/

install: build
	install -d $(PREFIX)/bin
	install kvlang $(PREFIX)/bin/kvlang

test:
	go test ./... -count=1

vet:
	go vet ./...

kvspace: build
	@redis-cli ping 2>/dev/null || redis-server --daemonize yes
	./kvlang kvspace clear

clean:
	go clean
	rm -f kvlang

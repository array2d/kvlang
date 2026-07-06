.PHONY: build test vet clean

GOPROXY ?= https://goproxy.cn,direct
OUT      := bin

build:
	go build -ldflags="-s -w" -o $(OUT)/kvlang ./cmd/vm/
	go build -ldflags="-s -w" -o $(OUT)/loader ./cmd/loader/

test:
	go test ./... -count=1

vet:
	go vet ./...

clean:
	go clean
	rm -rf $(OUT)

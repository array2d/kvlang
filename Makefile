.PHONY: build test vet clean redis

export GOPROXY ?= https://goproxy.cn,direct
OUT := bin

build:
	go mod tidy
	go build -ldflags="-s -w" -o $(OUT)/kvlang ./cmd/kvlang/

test:
	go test ./... -count=1

vet:
	go vet ./...

redis:
	@redis-cli ping 2>/dev/null || redis-server --daemonize yes
	redis-cli FLUSHALL

clean:
	go clean
	rm -rf $(OUT)

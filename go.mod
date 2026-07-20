module kvlang

go 1.24.4

require (
	github.com/array2d/kvspace-go v0.0.0-20260718144108-e2c451647dfa
	github.com/gorilla/websocket v1.5.3
)

require github.com/redis/go-redis/v9 v9.19.0 // indirect

require (
	github.com/cespare/xxhash/v2 v2.3.0 // indirect
	go.uber.org/atomic v1.11.0 // indirect
)
replace github.com/array2d/kvspace-go => ../kvspace-go

CC      = cc
CFLAGS  = -std=gnu11 -O2 -Wall -fPIC -D_GNU_SOURCE -Iinclude -Isrc
DUAL    = /home/peng.li24/github.com/array2d/kvspace-durable/target/release
KVS     = /home/peng.li24/github.com/array2d/kvspace/build
BLK     = /home/peng.li24/github.com/array2d/blockmalloc/build
SBO     = /home/peng.li24/github.com/array2d/slotsboxmalloc/build
SRCS    = src/strbuf.c src/xvalue.c src/kv.c src/keytree.c src/rwir.c \
          src/vthread.c src/logx.c src/builtin.c src/kvcpu.c src/runtime.c src/rwext.c
OBJS    = $(SRCS:.c=.o)
TARGET  = build/libkvlang_runtime.so

# 链接哪个 kvspace 库由 KVLANG_KVSPACE_LIB 控制（kvspace-c 或 kvspace_durable，均导出同一 ABI）
KVLANG_KVSPACE_LIB ?= kvspace_durable
ifeq ($(KVLANG_KVSPACE_LIB), kvspace-c)
  KVS_LINK = -L$(KVS) -lkvspace-c -L$(BLK) -lblockmalloc -L$(SBO) -lslotsboxmalloc \
             -Wl,-rpath,$(KVS) -Wl,-rpath,$(BLK) -Wl,-rpath,$(SBO)
else
  KVS_LINK = -L$(DUAL) -lkvspace_durable -Wl,-rpath,$(DUAL)
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	mkdir -p build
	$(CC) -shared -o $@ $(OBJS) $(KVS_LINK) -lm -pthread -Wl,--export-dynamic

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

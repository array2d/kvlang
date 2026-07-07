# kvlang

基于 Redis KV 空间的声明式编程语言 VM 解释器。

## 快速开始

```bash
make redis   # 启动 Redis + 清空数据
make build   # 构建 bin/kvlang + bin/loader
```

## 命令

| 二进制 | 说明 |
|--------|------|
| `bin/kvlang` | VM 服务端，常驻消费 vthread |
| `bin/loader` | 加载 .kv 源文件到 Redis |

## 示例

```kvlang
# example/kvlang/builtin/arith/three_add.kv
def three_add(A:int, B:int, C:int) -> (R:int) {
    A + B -> './t'
    './t' + C -> './R'
}
three_add(2, 3, 4) -> './out'
```

## Redis Schema

```
/vthread/<vtid>           vthread 状态 (pc, status)
/vthread/<vtid>/<pc>      指令数据 (reads/writes)
/src/func/<name>          函数定义
/sys/term/<name>          终端流配置
```

## 架构

```
.kv 源文件  ──▶  Lexer ──▶  Parser ──▶  AST ──▶  CodeGen
                                                      │
   Redis KV  ◀──  Loader  ◀───────────────────────────┘
      │
      ▼
    VM ──▶ builtin (原生求值)  /  dispatch (GPU 分发)
```

## License

MIT

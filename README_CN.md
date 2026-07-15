# kvlang

> English: [README.md](README.md)

基于 KV 路径寻址的声明式 VM 解释器。指令和数据共享同一棵树，label 即路径，call 即子树复制。

## 快速开始

```bash
make build    # 编译 kvlang
make kvspace  # 启动 Redis + 清空
```

## 使用

```bash
kvlang file.kv             # 执行 .kv 文件
kvlang -c 'print("hi")'    # 内联代码
echo 'code' | kvlang       # 管道输入
kvlang vet file.kv          # 语法检查
kvlang kvspace <cmd>        # KV 空间操作 (get/set/del/list/clear)
kvlang help                 # 帮助
```

## 语言速览

```kvlang
str.set("kvlangrun") -> './term'    # 激活终端输出

def add(A:int, B:int) -> (C:int) {
    A + B -> './C'
}
add(2, 3) -> './out'
```

### 控制流 (实验性)

```kvlang
def classify(flag:bool, X:int) -> (R:int) {
    X + 1 -> './a'
    if ('./flag') {
        './a' * 2 -> './b'
    } else {
        './a' * 3 -> './b'
    }
    './b' + 10 -> './R'
}
```

### Tensor (设计中)

```kvlang
tensor.new("f32", "[128]") -> /data/a
matmul(/data/W, /data/X)    -> /data/Y
tensor.del(/data/a)
```

## 内建算子

算术 `+ - * / %` · 比较 `== != < > <= >=` · 逻辑 `&& || !` · 数学 `abs pow sqrt min max` · 转换 `int float bool` · IO `print cerr` · 字符串 `str.set`

## 架构

```
.kv 源文件 ──▶ Lexer ──▶ Parser ──▶ AST
  │                                    │
  │  if/while/for  ──▶ lower ──▶ BlockStmt + br/goto
  │                                    │
  ▼                                    ▼
Register (签名)                WriteBody (KV 结构化)
  │                                    │
  └────────── kvspace KV ◀─────────────┘
                 │
                 ▼
               Execute ──▶ builtin (原生) / dispatch (GPU)
```

## KV 寻址

```
/vthread/<vtid>/<pc>/[i,0]    指令 opcode
/vthread/<vtid>/<pc>/[i,-1]   读参数
/vthread/<vtid>/<pc>/label/    控制流 block 子路径
/src/func/<name>/              函数体布局
/src/func/<name>/label/        block label 子函数
```

## License

MIT

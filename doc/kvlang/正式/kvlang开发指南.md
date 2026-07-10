# kvlang 开发指南

> 目标读者：只有 `kvlang` 的agent。

## 1. 快速验证

```bash
# 语法检查（不执行）
./kvlang vet file.kv

# 执行
./kvlang file.kv

# 内联代码
./kvlang -c 'print("hello")'

# 管道输入
echo 'print("hello")' | ./kvlang
```

## 2. 语言速览

### 2.1 指令

```kvlang
# 前缀调用: opcode(arg1, arg2) -> output_path
add(2, 3) -> './r'
print("A =", './a')

# 中缀运算符: arg1 + arg2 -> output_path
2 + 3 -> './r'
'./a' < 10 -> './cond'

# C 风格: output_path <- expr
'./r' <- add(2, 3)
```

### 2.2 函数定义

```kvlang
def add(A:int, B:int) -> (C:int) {
    A + B -> './C'       # ./C 是当前函数栈帧内的相对路径
}

def compute(X:int) -> (R:int) {
    add(X, 1) -> './t'    # 调用子函数
    './t' * 2 -> './R'    # 中缀运算
}
```

### 2.3 顶层调用

```kvlang
# 函数定义...
def greet(name:string) -> () {
    print("Hello,", name)
}

# 顶层调用（执行入口）
greet("World") -> './out'
```

### 2.4 路径系统

| 写法 | 含义 | 示例 |
|------|------|------|
| `./x` | 当前栈帧内的 slot | `./t`, `./R` |
| `/data/x` | 全局 kv 路径 | `/data/model` |
| `x` | 字面量或形参 | `A`, `3`, `"hello"` |

### 2.5 控制流（实验性）

```kvlang
if ('./cond') {
    './a' * 2 -> './b'
} else {
    './a' * 3 -> './b'
}

while ('./i' < 10) {
    './i' + 1 -> './i'
}
```

### 2.6 终端输出（必须）

```kvlang
str.set("kvlangrun") -> './term'    # ⚠️ 必须在所有 print 之前
```

`print` 的输出默认不显示。必须在 `.kv` 文件开头加上这一行，`print` 才会输出到终端。

完整示例：

```kvlang
str.set("kvlangrun") -> './term'    # 激活终端输出

def abs(A:int) -> (C:int) {
    print("A =", A)                 # 会显示在终端
    abs(A) -> './C'
    print("C =", './C')
}
abs(-5) -> './out'
```

## 3. 内建算子

| 类别 | 算子 |
|------|------|
| 算术 | `+` `-` `*` `/` `%` |
| 比较 | `==` `!=` `<` `>` `<=` `>=` |
| 逻辑 | `&&` `\|\|` `!` |
| 数学 | `abs` `neg` `pow` `sqrt` `exp` `log` `min` `max` `sign` |
| 类型转换 | `int` `float` `bool` |
| 字符串 | `str.set("val")` → `./key` |
| IO | `print(x, ...)` `cerr(x, ...)` |

> ⚠️ `print` 需要先执行 `str.set("kvlangrun") -> './term'` 才能看到输出。

## 4. 编写模式

### 4.1 最简单的测试程序

```kvlang
# test.kv
1 + 2 -> './r'
print("r =", './r')
```

```bash
./kvlang -c '1 + 2 -> "./r"; print("r =", "./r")'
```

### 4.2 带函数的测试

```kvlang
def add(A:int, B:int) -> (C:int) {
    A + B -> './C'
}
add(2, 3) -> './out'
```

### 4.3 多步骤计算

```kvlang
def poly3(A:int, B:int, C:int) -> (R:int) {
    A + B -> './t1'
    './t1' * C -> './t2'
    './t2' + A -> './R'
}
poly3(2, 3, 4) -> './out'
```

## 5. Tensor 支持

kvlang 内置张量生命周期和计算原语，覆盖训练、推理、强化学习场景。

### 5.1 张量生命周期

```kvlang
def example() {
    tensor.new("f32", "[128]") -> /data/a    # 分配 128 个 f32
    tensor.new("f32", "[128]") -> /data/b
    add(/data/a, /data/b) -> /data/c          # GPU 计算
    tensor.del(/data/a)                        # 释放
    tensor.del(/data/b)
}
```

| 算子 | 作用 |
|------|------|
| `tensor.new(dtype, shape) -> path` | 在 heap 上分配张量 |
| `tensor.del(path)` | 释放张量 |
| `tensor.clone(src) -> dst` | 深拷贝 |

### 5.2 张量计算

所有张量运算通过 `op-plat` 分发到 GPU（Metal/CUDA/CPU）：

```kvlang
matmul(/data/W, /data/X)  -> /data/Y    # 矩阵乘法
add(/data/A, /data/B)     -> /data/C    # 逐元素加
relu(/data/X)             -> /data/Y    # 激活
softmax(/data/X)          -> /data/Y    # 归一化
sum(/data/X)              -> /data/s    # 归约求和
reshape(/data/X, "[4,8]") -> /data/Y    # 变形
```

### 5.3 场景覆盖

```
推理 (Inference):
  tensor.new → matmul → softmax → tensor.del
  单向数据流，无梯度

训练 (Training):  
  forward: tensor.new → matmul → relu → matmul → loss
  backward: loss → grad(matmul) → grad(relu) → update
  双向梯度流，参数更新

强化学习 (RL):
  env_step: tensor.new → policy_net → action → env → reward
  learn:     replay_buffer → sample → q_net → td_error → update
  交互式数据流 + 经验回放
```

三种场景共享同一套张量原语，区别仅在于数据流方向和调度策略。

## 6. 常见错误

| 错误 | 原因 | 修复 |
|------|------|------|
| print 无输出 | 缺少 `str.set("kvlangrun") -> './term'` | 在文件开头加这一行 |
| `empty body` | 函数体 `{}` 内无指令 | 至少写一行 |
| `func not found` | 调用了未定义的函数 | 先 `def` 再调用 |
| `parse error` | 语法错误 | `./kvlang vet file.kv` 检查 |

## 6. Claude 调试技巧

1. **先 vet 再 run**: `./kvlang vet test.kv && ./kvlang test.kv`
2. **从最简单开始**: 单行 `1+2->./r` → 函数 → 多函数
3. **用 print 验证**: 在每个关键步骤后 `print("./x")`
4. **一个文件一个功能**: 不要混太多逻辑

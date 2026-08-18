# kvlang 速览

kvlang 是一门全新语言，语法以下面示例为准，不要套用其它语言直觉。

## 程序结构
顶层把代码包进 `rwfunc main() -> () { … }`，最后单独调用 `main()`。别把函数命名为 `init`。
```kv
rwfunc main() -> () {
    total = 0            # = 等价 <-，写左边这个槽
    1 -> i               # -> 写右边这个槽
    while (i <= 5) { total <- total + i; i + 1 -> i }
    println(total)       # 15
}
main()
```

## 赋值三形式与调用
- `x = e` / `x <- e`：写左边的槽；`e -> x`：写右边的槽。`=` 不是表达式，不能嵌进条件里。
- 调用只能通过写参映射拿结果，没有返回值：`f(a,b) -> r`；丢弃 `-> _`；多个 `-> x, y`。
- 写槽必须是位置：裸名（帧局部）、`/绝对/路径`（全局）、`base.name`（成员）。字面量不能当写槽。

## rwfunc 读参/写参
- 读参 `(a:int64)` 只读，函数体内不能把读参放进写槽（含 `a[i] <- v`）。
- 写参 `-> (acc:int64)` 体内可读可写——累加器、要被修改的数组都声明为写参。
- **写参初值是 None，不是 0**：累加前必须先显式 `0 -> acc`，否则首次 `acc + x` 是 `None + x` 直接失败。
```kv
lib mylib { rwfunc add(A:int64, B:int64) -> (C:int64) { A + B -> C } }
rwfunc sum(arr:[]int64) -> (acc:int64) {
    0 -> acc; 0 -> i
    while (i < len(arr)) { at(arr,i) -> e; acc + e -> acc; i + 1 -> i }
}
rwfunc main() -> () {
    mylib.add(3,4) -> s; println(s)          # 7
    a:[]int64 = [1,2,3,4,5]; sum(a) -> t; println(t)   # 15
}
main()
```

## 数值类型（只有定宽类型）
`int8/16/32/64 uint8/16/32/64 float32/64` 及 `char/utf8` 等；**没有 `int`/`float`**（会被拒）。
构造器兼转换：`float32(3)`、`int8(300)`（窄化按补码回绕，float→int 向零截断）。变量声明 `x:int64 = 42`。

## 数组（关键：`[]` 前缀才是数组，裸 kind 是标量）
```kv
a:[]int64 = [7,2,9,4]     # ✅ 一维数组。写成 a:int64=[...] 会报错（那是标量）
len(a) -> n               # 4
at(a,2) -> e              # 9（0 起）
set(a,1,99) -> a          # 改元素：a=[7,99,9,4]
```
遍历/聚合用 `while + len + at`。求最大值：
```kv
at(a,0) -> hi; 1 -> i
while (i < len(a)) { at(a,i) -> e; if (e > hi) { e -> hi }; i + 1 -> i }   # hi=9
```

## dict / 成员 / 指针
```kv
d = { name="kv"; ver=1 }   # 成员是扁平键族 d.name d.ver
d.name -> x; d.ver = 2
k = "name"; d.*k -> v      # 动态键：读 d.name
/n1 = { val=1; next="/n2" }  # 跨函数共享的数据放绝对路径
"/n1" -> p; p.val -> v       # 变量存路径字符串 = 指针，.member 解引用
```

## 控制流（只能在 rwfunc 体内）
`while (c) {…}`、`if (c) {…} else {…}`。条件可为复合表达式。数组遍历用 `while` 配 `len`/`at`（见上），不要用 for-in。

## 运算符（注意乘除号）
算术 `+ - × ÷ %`：**乘用 `×`、除用 `÷`，不要用 `*` `/`**（`/` 只用于路径，`*` 保留）。
`÷`：两整数为整除（`7÷2`=3），任一为浮点则浮点除。比较 `== != < > <= >=`，逻辑 `&& || !`。

## IO 与字符串
`print` / `println` 可用（扩展 rwir）。字符串用 `+` 拼接：
```kv
s = "hello"
string.len(s) -> n            # 5
string.char(s,1) -> c         # 'e'（读第 i 个字符）
string.slice(s,0,2) -> p      # "he"
string.find(s,"ll") -> i      # 2（找不到返回 -1）
# 替换第 i 个字符：用 slice 拼接，不要用 string.char(s,i)=x
string.slice(s,0,1) + "a" -> t; string.slice(s,2,5) -> u; t + u -> r   # "hallo"
```

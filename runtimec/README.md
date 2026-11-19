# runtime

+ 设计者：miaobyte
+ 规则制定级别：最高
+ 设计日期：2026/11/13

## 背景

runtime模块，是kvlang最重要的执行模块

runtime经历过四个版本的设计

+ oldhero/go的kvcpu,第一个go语言实现的kvcpu，按照每一个明文rwir都是kvcpu的指令，纯解释执行。
+ oldhero/runtime，第一个c实现的版本，思想复刻go的kvcpu。后期加入了fastcode，诞生了kvlang的jit的雏形。
+ oldhero/runtimec2，第一个c++实现的版本，同时设计了vmkv（同kvcpu）+vmbin（binir）的双栈，加入了binir，加入了jit。但是执行顺序混乱，只有很少的代码支持jit，最大的run_frame有接近3000行代码。benchmark中0.3.30前的一些高分数据是c2实现的
+ runtimec，也就是当前版本。彻底放弃了vmkv，kv明文的rwir全部要被编译为binir。

## 反思
ai（claude+deepseek），很擅长完成明确输入输出的任务，如layout的代码，只要正确的把我的kv代码，layout到正确的kvspace上就可以，kvspace同样是明文可查询的。但是对runtimec，它非常不擅长。

1.没有架构自洽思考。
2.没有深度理解kvspace提供了程序栈，而runtimec本质的工作是kvcpu本身。
3.我来写解释器时，额外定义一个char[256],都会倍加思考很多遍，这块栈内存该不该被申请，多一行代码我都极其小心谨慎，多一行ifelse判断，我都会⬇想如何通过语言系统架构设计避免这个ifelse判断。而ai常常不care，随手制造大片的栈变量，大量的无用判断，不仔细看kvlang的约束设计，就加一堆无用的兜底，导致解释器一条rwir的handler非常复杂+冗余。成为垃圾代码，只能被我丢弃。

因此，我不得不使用cursor风格的监督模式，盯着codeagent实现的每一行runtimec代码。

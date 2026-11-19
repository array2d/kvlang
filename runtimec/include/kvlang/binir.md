
+ 设计者：miaobyte
+ 规则制定级别：中，可能迭代
+ 设计日期：2026/11/12



```
typedef struct binir {
    uint64_t opcode : 16;//opcode id，虽然目前的数量是 1845，后续可能会压缩。
    uint64_t kvpc_s0adder : 16;//kvpc的增量，kvpc是kvlang的程序计数器，binir指令执行完后，kvpc会增加这个值。
                             // o0优化模式，这个值通常是1；
                             // 在o1优化模式下，这个值是可能是0，也可能更大，取决于编译器；
    uint64_t payload_count : 24;//binir的操作数数量，每个操作数都是8byte。payload区总长度=8*payload_count

    uint64_t reserved : 8;
} binir_t;

typedef struct binir {
    uint64_t opcode : 16;//opcode id，虽然目前的数量是 1845，后续可能会压缩。
    uint64_t kvpc_s0adder : 16;//kvpc的增量，kvpc是kvlang的程序计数器，binir指令执行完后，kvpc会增加这个值。
                             // o0优化模式，这个值通常是1；
                             // 在o1优化模式下，这个值是可能是0，也可能更大，取决于编译器；
    uint64_t payload_count : 24;//binir的操作数数量，每个操作数都是8byte。payload区总长度=8*payload_count

    uint64_t reserved : 8;
} binir_t;

```

这一轮，我做了以下设计改动
1.不再区分r、w读写参
2.大幅拓宽参数区的长度M,参数区总byte=8*M，以支持放置jit区产物。
3.M本身也代表了下一条binir指令的跳数
3.kvpc_adder，则是kvspace的pcs0指令轴需要增加的数。在o0模式下，不会有jit，不会有优化，这个值默认就是1；
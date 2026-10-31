// 打印 myrwircap 唯一能力表的全部 opcode（每行一个），供 CI 差集断言使用。
#include <cstdio>

#include "rwir.hpp"

int main() {
    for (int i = 0; i < kvlang::runtimec2::rwir::kMyrwircapsN; i++)
        std::puts(kvlang::runtimec2::rwir::myrwircaps[i].name);
    return 0;
}

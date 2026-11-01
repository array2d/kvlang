// 打印 myrwircap 唯一能力表的全部 opcode（每行一个），供 CI 差集断言使用。
// `--union`：附带阶段 A 的 union 签名（操作数位图 / arity / 结果规则），诊断用；
// 默认输出保持一行一个 opcode，差集断言不受影响。
#include <cstdio>
#include <cstring>

#include "rwir.hpp"
#include "rwir_signature.hpp"

int main(int argc, char** argv) {
    using namespace kvlang::runtimec2::rwir;
    const bool verbose = argc > 1 && std::strcmp(argv[1], "--union") == 0;
    for (int i = 0; i < kMyrwircapsN; i++) {
        const Entry& e = myrwircaps[i];
        if (!verbose) {
            std::puts(e.name);
            continue;
        }
        const OpSignature* sig = e.signature ? e.signature : signature_of(e.op);
        if (!sig) {
            std::printf("%s\t-\n", e.name);
            continue;
        }
        std::printf("%s\toperands=0x%04x arity=%u..%u result=%u\n", e.name,
                    static_cast<unsigned>(sig->operands),
                    static_cast<unsigned>(sig->min_arity),
                    static_cast<unsigned>(sig->max_arity),
                    static_cast<unsigned>(sig->result));
    }
    return 0;
}

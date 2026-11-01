// 原地写守卫：head 声明 body_len=4（slack），物理 body_cap=64。
// 8 字节 int64 直写必须被拒绝，不得越过 body 污染 attr 区。
#include <cstdio>
#include <cstring>

#include "vmkv.hpp"
#include "vmbin/frame.hpp"

using namespace kvlang::runtimec2;

int main() {
    vmbin::Frame frame;
    frame.regs.resize(1);
    vmbin::Register& reg = frame.regs[0];

    // 映射布局：head [0,8) / body [8,12) / attr [12,22) 连续。
    std::uint8_t mem[32];
    std::memset(mem, 0, sizeof mem);
    const char attr[] = "\x0a\x00\x01\x05\x00int64"; /* tag1 langtype */
    std::memcpy(mem + 12, attr, sizeof attr - 1);
    mem[0] = 4; /* body_len（u48 低字节） */
    mem[6] = 1; /* storetype SLACK */

    reg.view.head = mem;
    reg.view.headlen = 8;
    reg.view.body = mem + 8;
    reg.view.body_cap = 64;
    reg.view_ok = true;
    reg.body = mem + 8;
    reg.cap = 4; /* reg_body 缓存的 body 逻辑长度 */
    reg.key.clear(); /* fallback 不触 kvspace */

    std::uint8_t attr_before[10];
    std::memcpy(attr_before, mem + 12, sizeof attr_before);
    const bool ok = g_vmkv.store_i64_slot(frame, reg, 0x1122334455667788LL);
    const bool attr_same =
        std::memcmp(attr_before, mem + 12, sizeof attr_before) == 0;
    std::printf("store_ok=%d attr_same=%d\n", ok, attr_same);
    if (ok || !attr_same) {
        std::puts("FAIL: 8-byte write crossed body_len into attr");
        return 1;
    }
    std::puts("OK: rejected before touching attr");
    return 0;
}

// 槽状态状态机单测（对应成员读优化「用例 4」）：handler 直写 kvspace 后，
// 只有写的正是本寄存器自己的 key 才回到 Present；派生 key（成员/动态键）
// 不动寄存器自身状态——Absent 一旦被错误清除，后续写就会多一次类型查询
// （性能问题）；Absent 一旦被错误保留，后续 int64 写会跳过类型查询而把
// float64/struct 槽当成纯内存写（正确性问题）。
#include <cstdio>
#include <string>

#include "vmbin/frame.hpp"

using kvlang::runtimec2::vmbin::note_slot_write;
using kvlang::runtimec2::vmbin::Register;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

using State = Register::SlotState;

const char* state_name(State state) {
    switch (state) {
    case State::Unknown: return "Unknown";
    case State::Absent: return "Absent";
    case State::Present: return "Present";
    }
    return "?";
}

void expect_state(const Register& reg, State want, const char* message) {
    if (reg.slot_state == want)
        return;
    std::fprintf(stderr, "FAIL: %s (got %s want %s)\n", message,
                 state_name(reg.slot_state), state_name(want));
    ++failures;
}

Register make_register(const char* key, State state) {
    Register reg;
    reg.key = key;
    reg.slot_state = state;
    return reg;
}

}  // namespace

int main() {
    {
        Register reg;  // 默认保守：一切未知
        expect_state(reg, State::Unknown, "default state must be Unknown");
    }
    {
        Register reg = make_register("/f/x", State::Absent);
        note_slot_write(reg, "/f/x");
        expect_state(reg, State::Present, "own-key write must clear Absent");
    }
    {
        Register reg = make_register("/f/x", State::Absent);
        note_slot_write(reg, "/f/x·val");
        expect_state(reg, State::Absent, "member write must keep the slot Absent");
    }
    {
        Register reg = make_register("/f/x", State::Absent);
        note_slot_write(reg, nullptr);
        note_slot_write(reg, "");
        expect_state(reg, State::Absent, "empty key must not clear Absent");
    }
    {
        Register reg = make_register("/f/x", State::Present);
        note_slot_write(reg, "/f/x");
        expect_state(reg, State::Present, "Present must stay Present");
    }
    {
        Register reg = make_register("/f/x", State::Unknown);
        note_slot_write(reg, "/f/x");
        expect_state(reg, State::Unknown, "Unknown must not be promoted by a write");
    }
    if (failures != 0)
        return 1;
    std::puts("OK slot state transitions");
    return 0;
}

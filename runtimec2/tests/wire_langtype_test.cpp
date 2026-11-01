// Unit tests for bounded wire attr `langtype` parsing on truncated/malformed buffers.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include <kvspace/xvalue_head.h>
#include "value.hpp"

using kvlang::runtimec2::wire_langtype_view;

namespace {

int failures = 0;

void check(bool cond, const char* text, int line) {
    if (!cond) {
        std::printf("FAIL line %d: %s\n", line, text);
        failures++;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

}  // namespace

int main() {
    // 1. Valid wire: 8B head (body_len=8) + 8B body + 10B attr ("int64")
    {
        std::vector<std::uint8_t> buf(26, 0);
        kvspaceXvalueHeadSetLen(reinterpret_cast<kvspaceXvalueHead*>(buf.data()), 8);
        const std::size_t a = 16;
        buf[a + 0] = 10;
        buf[a + 1] = 0;
        buf[a + 2] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        buf[a + 3] = 5;
        buf[a + 4] = 0;
        std::memcpy(buf.data() + a + 5, "int64", 5);
        const std::uint8_t* p = nullptr;
        const std::uint16_t n = wire_langtype_view(buf.data(), buf.size(), &p);
        CHECK(n == 5 && p != nullptr &&
              std::string_view(reinterpret_cast<const char*>(p), n) == "int64");
    }

    // 2. Truncated buffer (< 10B)
    {
        std::uint8_t buf[9] = {};
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(1);
        CHECK(wire_langtype_view(buf, sizeof buf, &p) == 0 && p == nullptr);
        CHECK(wire_langtype_view(nullptr, 100, &p) == 0 && p == nullptr);
    }

    // 3. Oversized `body_len` (> `total_len`): must not dereference `head + 8 + body_len`
    {
        std::vector<std::uint8_t> buf(16, 0);
        kvspaceXvalueHeadSetLen(reinterpret_cast<kvspaceXvalueHead*>(buf.data()),
                                0x100000000ULL);
        const std::uint8_t* p = nullptr;
        CHECK(wire_langtype_view(buf.data(), buf.size(), &p) == 0 && p == nullptr);
    }

    // 4. `attr_len` exceeding remaining buffer (`attr_len = 200`, 4B left)
    {
        std::vector<std::uint8_t> buf(12, 0);
        kvspaceXvalueHeadSetLen(reinterpret_cast<kvspaceXvalueHead*>(buf.data()), 0);
        buf[8] = 200;
        buf[9] = 0;
        buf[10] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        buf[11] = 5;
        const std::uint8_t* p = nullptr;
        CHECK(wire_langtype_view(buf.data(), buf.size(), &p) == 0 && p == nullptr);
    }

    // 5. Invalid `attr_len < 2` (0 or 1)
    {
        std::vector<std::uint8_t> buf(12, 0);
        buf[8] = 1;
        buf[9] = 0;
        const std::uint8_t* p = nullptr;
        CHECK(wire_langtype_view(buf.data(), buf.size(), &p) == 0 && p == nullptr);
    }

    // 6. `record_len` exceeding end of attr section
    {
        std::vector<std::uint8_t> buf(16, 0);
        buf[8] = 8;  // attr_len = 8 (end = 16)
        buf[9] = 0;
        buf[10] = KVSPACE_XVALUE_ATTR_TAG_LANGTYPE;
        buf[11] = 10;  // rlen = 10 > 8 - 2 - 3
        buf[12] = 0;
        const std::uint8_t* p = nullptr;
        CHECK(wire_langtype_view(buf.data(), buf.size(), &p) == 0 && p == nullptr);
    }

    if (failures == 0)
        std::printf("wire_langtype_test: all checks passed\n");
    return failures ? 1 : 0;
}

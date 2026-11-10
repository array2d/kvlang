// E 层（#583 L3）单测：RegisterDesc 尺寸、kindexpr 映射、类型合并规则、None 回退。
// 只依赖头文件，不链接 kvspace/runtime。
#include <cstdint>
#include <cstdio>
#include <string>

#include "vmbin/program.hpp"
#include "vmbin/regtype.hpp"

using namespace kvlang::runtimec2::vmbin;

namespace {

int failures = 0;

void check(bool condition, const char* text, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, text);
        failures++;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

}  // namespace

int main() {
    // 1. 热结构尺寸：+2B 必须落进尾部 padding。
#if defined(__GLIBCXX__) || defined(_GLIBCXX_RELEASE)
    CHECK(sizeof(RegisterDesc) == 80);
#elif defined(_LIBCPP_VERSION)
    CHECK(sizeof(RegisterDesc) == 64);
#endif

    // 2. kindexpr → LangType/width（只认无 dims 前缀的标量/字符族）。
    LangType kind = LangType::None;
    std::uint8_t width = 0;
    CHECK(langtype_kind("int64", &kind, &width) && kind == LangType::Int64 &&
          width == 8);
    CHECK(langtype_kind("uint16", &kind, &width) && kind == LangType::Uint16 &&
          width == 2);
    CHECK(langtype_kind("float32", &kind, &width) && kind == LangType::Float32 &&
          width == 4);
    CHECK(langtype_kind("bool", &kind, &width) && kind == LangType::Bool &&
          width == 1);

    // 数组/字符串/指针/容器/空 → 回退（必须保持 None）。
    // 回归守卫（xv_meta）：字符串完整类型恒带 `[N]` dims；声明处的裸
    // `char/utf8` 只是元素类型，不能当完整 langtype 缓存，否则打印丢 `[2]`。
    CHECK(!langtype_kind("[8,8]float64", &kind, &width));
    CHECK(!langtype_kind("[2]char/utf8", &kind, &width));
    CHECK(!langtype_kind("char/utf8", &kind, &width));
    CHECK(!langtype_kind("char/utf32", &kind, &width));
    CHECK(!langtype_kind("*int64", &kind, &width));
    CHECK(!langtype_kind("[int64]\xC2\xB7int64", &kind, &width));
    CHECK(!langtype_kind("", &kind, &width));

    // 3. LangType → 规范名；不可表示 → nullptr（None 回退语义）。
    CHECK(std::string(langtype_name(LangType::Int64)) == "int64");
    CHECK(std::string(langtype_name(LangType::Float64)) == "float64");
    CHECK(langtype_name(LangType::None) == nullptr);
    CHECK(langtype_name(LangType::Array) == nullptr);
    CHECK(langtype_name(LangType::StringKeyMap) == nullptr);
    // char 族不可由静态元素 kind 还原（需要 `[N]` dims）。
    CHECK(langtype_name(LangType::CharUtf8) == nullptr);
    CHECK(langtype_name(LangType::CharUtf32) == nullptr);

    // 4. 合并规则。
    {  // 写者一致 + 读槽一致
        RegisterTypeState state;
        state.record(LangType::Int64, 8, true);
        state.record(LangType::Int64, 8, false);
        CHECK(state.final_kind(&width) == LangType::Int64 && width == 8);
    }
    {  // 任一写者无类型 → None
        RegisterTypeState state;
        state.record(LangType::Int64, 8, true);
        state.record(LangType::None, 0, true);
        CHECK(state.final_kind(&width) == LangType::None && width == 0);
    }
    {  // 写者类型冲突 → None
        RegisterTypeState state;
        state.record(LangType::Int64, 8, true);
        state.record(LangType::Float64, 8, true);
        CHECK(state.final_kind(&width) == LangType::None);
    }
    {  // 写者宽度冲突 → None
        RegisterTypeState state;
        state.record(LangType::Int64, 8, true);
        state.record(LangType::Int64, 4, true);
        CHECK(state.final_kind(&width) == LangType::None);
    }
    {  // 只读变量：读槽一致 → 采用
        RegisterTypeState state;
        state.record(LangType::Float64, 8, false);
        CHECK(state.final_kind(&width) == LangType::Float64 && width == 8);
    }
    {  // 只读变量：读槽冲突 → None
        RegisterTypeState state;
        state.record(LangType::Float64, 8, false);
        state.record(LangType::Int64, 8, false);
        CHECK(state.final_kind(&width) == LangType::None);
    }
    {  // 从未出现 → None（回退 GetHead）
        RegisterTypeState state;
        CHECK(state.final_kind(&width) == LangType::None);
    }

    std::printf("register_desc_type_test: %s (%d failures)\n",
                failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}

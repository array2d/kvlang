// Unit test for the standalone scalar JIT matrix.
// 已注册为 ctest `c2_jit_scalar_matrix`；矩阵本身尚未接线（阶段 B）。
#include "jit_scalar_matrix.hpp"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace kvlang::runtimec2::jit2;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

const ScalarTypeClass kTypes[] = {
    ScalarTypeClass::SI8, ScalarTypeClass::SI16, ScalarTypeClass::SI32,
    ScalarTypeClass::SI64, ScalarTypeClass::UI8, ScalarTypeClass::UI16,
    ScalarTypeClass::UI32, ScalarTypeClass::UI64, ScalarTypeClass::F32,
    ScalarTypeClass::F64,
};
const char* const kTypeNames[] = {"si8", "si16", "si32", "si64", "ui8",
                                  "ui16", "ui32", "ui64", "f32", "f64"};

std::string signature(const ScalarPlan& p) {
    std::string s;
    for (int i = 0; i < p.count; i++)
        s += std::to_string(static_cast<int>(p.ops[i])) + ",";
    return s;
}

// Template signature: width-specific narrow load/store collapse to one class,
// so SI8/16/32 share a structural template (the 40-60 leaf view).
std::string template_signature(const ScalarPlan& p) {
    std::string s;
    for (int i = 0; i < p.count; i++) {
        MicroOp m = p.ops[i];
        int v = static_cast<int>(m);
        switch (m) {
        case MicroOp::LoadS8: case MicroOp::LoadS16: case MicroOp::LoadS32:
            v = 1000; break;
        case MicroOp::LoadZ8: case MicroOp::LoadZ16: case MicroOp::LoadZ32:
            v = 1001; break;
        case MicroOp::Store8: case MicroOp::Store16: case MicroOp::Store32:
            v = 1002; break;
        default: break;
        }
        s += std::to_string(v) + ",";
    }
    return s;
}

}  // namespace

int main() {
    int typed_covered = 0, typed_rejected = 0;
    int byte_min = 1000, byte_max = 0;
    int over30 = 0;
    std::set<std::string> structures;
    std::set<std::string> templates;
    std::vector<std::string> rejected;

    for (int o = 0; o < static_cast<int>(KernelOp::Cast); o++) {
        const KernelOp op = static_cast<KernelOp>(o);
        for (int t = 0; t < 10; t++) {
            if (!typed_variant_exists(op, kTypes[t])) continue;
            ScalarPlan p = scalar_plan(op, kTypes[t], kTypes[t], kTypes[t]);
            if (!p.ok) {
                typed_rejected++;
                rejected.push_back(std::string(p.leaf) + " : " +
                                   (p.reject_reason ? p.reject_reason : "?"));
                continue;
            }
            typed_covered++;
            if (p.bytes_min < byte_min) byte_min = p.bytes_min;
            if (p.bytes_max > byte_max) byte_max = p.bytes_max;
            if (p.bytes_max > 30) over30++;
            structures.insert(signature(p));
            templates.insert(template_signature(p));
        }
    }

    // Casts: all source x target (10 x 10) must have a plan.
    int cast_cells = 0;
    for (int s = 0; s < 10; s++) {
        for (int d = 0; d < 10; d++) {
            ScalarPlan p = scalar_plan(KernelOp::Cast, kTypes[s], kTypes[s],
                                       kTypes[d]);
            if (!p.ok) {
                std::fprintf(stderr, "cast %s->%s rejected: %s\n", kTypeNames[s],
                             kTypeNames[d], p.reject_reason ? p.reject_reason : "?");
                g_failures++;
            } else {
                cast_cells++;
                structures.insert("cast:" + signature(p));
                templates.insert("cast:" + template_signature(p));
            }
        }
    }

    // Promotion table: spec 05-runtime语义/11-指令派发与特化.kv §数值 promotion。
    // want = nullptr 表示该组合不特化（混合符号 int×uint），返回 None。
    struct Pair { const char* a; const char* b; const char* want; };
    const Pair pairs[] = {
        {"int8", "int16", "int16"},
        {"int32", "int64", "int64"},
        {"int64", "float32", "float32"}, {"int64", "float64", "float64"},
        {"float32", "float64", "float64"},
        {"int8", "uint8", nullptr}, {"int32", "uint32", nullptr},
        {"int64", "uint64", nullptr}, {"uint16", "int32", nullptr},
    };
    for (const Pair& p : pairs) {
        const ScalarTypeClass got = promote(parse_kindexpr(p.a),
                                            parse_kindexpr(p.b));
        const ScalarTypeClass want = p.want ? parse_kindexpr(p.want)
                                            : ScalarTypeClass::None;
        if (got != want) {
            std::fprintf(stderr, "promote %s+%s -> class %d, want %s\n", p.a,
                         p.b, static_cast<int>(got),
                         p.want ? p.want : "none");
            g_failures++;
        }
    }

    // Print a compact matrix: op x type, leaf byte range.
    std::printf("op\\type    ");
    for (int t = 0; t < 10; t++) std::printf("%-10s", kTypeNames[t]);
    std::printf("\n");
    for (int o = 0; o < static_cast<int>(KernelOp::Cast); o++) {
        const KernelOp op = static_cast<KernelOp>(o);
        const char* opn = "?";
        switch (op) {
        case KernelOp::Add: opn = "add"; break;
        case KernelOp::Sub: opn = "sub"; break;
        case KernelOp::Mul: opn = "mul"; break;
        case KernelOp::Div: opn = "div"; break;
        case KernelOp::Mod: opn = "mod"; break;
        case KernelOp::Max: opn = "max"; break;
        case KernelOp::Min: opn = "min"; break;
        case KernelOp::Abs: opn = "abs"; break;
        case KernelOp::Neg: opn = "neg"; break;
        case KernelOp::Sign: opn = "sign"; break;
        case KernelOp::BitAnd: opn = "bitand"; break;
        case KernelOp::BitOr: opn = "bitor"; break;
        case KernelOp::BitXor: opn = "bitxor"; break;
        case KernelOp::Shl: opn = "shl"; break;
        case KernelOp::Shr: opn = "shr"; break;
        case KernelOp::Eq: opn = "eq"; break;
        case KernelOp::Neq: opn = "neq"; break;
        case KernelOp::Lt: opn = "lt"; break;
        case KernelOp::Le: opn = "le"; break;
        case KernelOp::Gt: opn = "gt"; break;
        case KernelOp::Ge: opn = "ge"; break;
        default: break;
        }
        std::printf("%-10s", opn);
        for (int t = 0; t < 10; t++) {
            ScalarPlan p = scalar_plan(op, kTypes[t], kTypes[t], kTypes[t]);
            if (p.ok) {
                char cell[24];
                std::snprintf(cell, sizeof(cell), "%u-%uB", p.bytes_min,
                              p.bytes_max);
                std::printf("%-10s", cell);
            } else {
                std::printf("%-10s", "-");
            }
        }
        std::printf("\n");
    }
    std::printf("cast       ");
    for (int t = 0; t < 10; t++) {
        ScalarPlan p = scalar_plan(KernelOp::Cast, ScalarTypeClass::SI64,
                                   ScalarTypeClass::SI64, kTypes[t]);
        char cell[24];
        std::snprintf(cell, sizeof(cell), "%u-%uB", p.bytes_min, p.bytes_max);
        std::printf("%-10s", cell);
    }
    std::printf("\n");

    std::printf("\ntyped_covered=%d typed_rejected=%d cast_cells=%d "
                "unique_structures=%zu templates=%zu bytes=%d..%dB over30=%d\n",
                typed_covered, typed_rejected, cast_cells, structures.size(),
                templates.size(), byte_min, byte_max, over30);
    for (const std::string& r : rejected)
        std::printf("rejected: %s\n", r.c_str());
    std::printf("fails=%d\n", g_failures);
    return g_failures == 0 ? 0 : 2;
}

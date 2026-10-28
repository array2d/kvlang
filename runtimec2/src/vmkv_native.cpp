#include "vmkv_native.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <sys/random.h>
#include <vector>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "value.hpp"
#include "valueio.hpp"

namespace kv::rt {

namespace {
const bool g_trace = std::getenv("RC2_TRACE") != nullptr;
}  // namespace
using bin::FReg;
using bin::FrameCtx;
using bin::RunResult;

// ── NativeCtx 方法（原 run_frame 内嵌 lambda 提升）────────────────────────
bool NativeCtx::ai64(int i, std::int64_t* v) {
    Scalar sc{};
    if (v_scalar(in[i], &sc)) {
        *v = sc.k == Scalar::K::Int     ? sc.i
             : sc.k == Scalar::K::Float ? (std::int64_t)sc.f
                                        : (std::int64_t)sc.u;
        return true;
    }
    // time/duration 等非数值 langtype：body 即 8 字节整数。
    kvspaceXh xh{};
    if (kvspaceXhDecode(in[i].d, in[i].n, &xh) != 0 ||
        xh.content_len != 8) {
        if (g_trace)
            std::fprintf(stderr, "[ai64fail i=%d d=%p n=%u]\n", i,
                         (void*)in[i].d, in[i].n);
        return false;
    }
    std::memcpy(v, xh.body, 8);
    return true;
}

bool NativeCtx::atext(int i, std::string* out) {
    return wire_text(in[i].d, in[i].n, out);
}

bool NativeCtx::wbool(bool b) {
    Scalar sc{};
    sc.k = Scalar::K::Bool;
    sc.width = 1;
    sc.u = b ? 1 : 0;
    return vm.slot_write_scalar(f, ins.wr[0], sc, 1);
}

bool NativeCtx::path_arg(int i, std::uint8_t rno, const RegDesc& d,
                         std::string* out) {
    // 值文本优先（对齐 C path_arg）：读参值若是 char/ 且 `/` 开头 → 直接
    // 用文本（`·`→`/`）。参数（Ptr）的 key 解析出来是 arg 槽链，不是路径
    // 文本，列表 op 必须用值（kvspace·listlen(prefix) 参数值 `/tmp/kvt/`）。
    Wire vw{};
    std::string lt, txt;
    if (vm.read_reg(f, rno, &vw) && v_langtype(Wire{vw.d, vw.n}, &lt) &&
        lt.find("char/") != std::string::npos &&
        wire_text(vw.d, vw.n, &txt) && !txt.empty() && txt[0] == '/') {
        *out = txt;  // 路径文本原样（`·` 保留，对齐 C path_arg）
        return true;
    }
    if (d.kind == Operand::Kind::Literal) {
        return atext(i, out);  // in[] 按参数序号
    }
    if (d.name[0] == '/') {
        *out = d.name.substr(0, d.name.find(' '));  // 剥类型后缀
        return true;
    }
    FReg& pr = f.regs[rno];
    if (!vm.ensure_reg_key(f, pr))
        return false;
    if (g_trace)
        std::fprintf(stderr,
                     "[patharg var i=%d key='%s' desc='%s' refn='%s']\n",
                     i, pr.key.c_str(), pr.desc.c_str(),
                     pr.desc_ref ? pr.desc_ref->name.c_str() : "-");
    *out = pr.key;
    return true;
}

bool NativeCtx::kv_paths(std::string* k0, std::string* k1) {
    if (g_trace)
        std::fprintf(stderr,
                     "[patharg rd0=%d kind=%d name='%s' sk='%s']\n",
                     (int)ins.rd[0], (int)seg->regs[ins.rd[0]].kind,
                     seg->regs[ins.rd[0]].name.c_str(),
                     seg->regs[ins.rd[0]].slot_key.c_str());
    if (!path_arg(0, ins.rd[0], seg->regs[ins.rd[0]], k0))
        return false;
    if (ins.nrd >= 2 &&
        !path_arg(1, ins.rd[1], seg->regs[ins.rd[1]], k1))
        return false;
    return true;
}

std::string NativeCtx::write_slot_lt() {
    // 写槽描述符 body = `name\0<langtype>`（NUL 分隔两段）。
    const std::string wkey =
        f.func_dir + "/[" + std::to_string(ins.s0) + ",1]";
    const std::uint8_t* wd = nullptr;
    std::uint32_t wn = 0;
    if (read_wire_raw(f.h, wkey, &wd, &wn) && wd) {
        kvspaceXh wxh{};
        if (kvspaceXhDecode(wd, wn, &wxh) == 0 && wxh.body) {
            const std::uint8_t* b = wxh.body;
            const std::size_t bl = wxh.content_len;
            if (bl > 5) {
                const char* p = reinterpret_cast<const char*>(b + 5);
                const std::size_t cap = bl - 5;
                std::size_t i = 0;
                while (i < cap && p[i] != '\0') i++;
                if (i < cap)
                    return std::string(p + i + 1, cap - i - 1);
            }
        }
    }
    return {};
}

std::string NativeCtx::parse_dims(std::string lt, std::int32_t dims[8],
                                  std::uint32_t* ndim) {
    *ndim = 0;
    if (!lt.empty() && lt[0] == '[') {
        const std::size_t close = lt.find(']');
        if (close != std::string::npos) {
            const std::string seg = lt.substr(1, close - 1);
            std::size_t i = 0;
            while (i <= seg.size() && *ndim < 8) {
                const std::size_t comma = seg.find(',', i);
                const std::string one =
                    comma == std::string::npos ? seg.substr(i)
                                               : seg.substr(i, comma - i);
                char* end = nullptr;
                dims[(*ndim)++] = static_cast<std::int32_t>(
                    std::strtoll(one.c_str(), &end, 10));
                if (comma == std::string::npos)
                    break;
                i = comma + 1;
            }
            return lt.substr(close + 1);  // 剩裸 kind（如 float64）
        }
    }
    return lt;
}

std::uint32_t NativeCtx::elem_size(const std::string& kind) {
    if (kind.rfind("float64", 0) == 0) return 8;
    if (kind.rfind("float32", 0) == 0) return 4;
    if (kind.rfind("int64", 0) == 0 || kind.rfind("uint64", 0) == 0) return 8;
    if (kind.rfind("int32", 0) == 0 || kind.rfind("uint32", 0) == 0) return 4;
    if (kind.rfind("int16", 0) == 0 || kind.rfind("uint16", 0) == 0) return 2;
    if (kind.rfind("int8", 0) == 0 || kind.rfind("uint8", 0) == 0 ||
        kind.rfind("bool", 0) == 0) return 1;
    return 0;
}

namespace {

// ── time ──────────────────────────────────────────────────────────────────
bool h_time_now(NativeCtx& c, std::string*) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    const std::int64_t ns =
        (std::int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "time", ns);
}

bool h_time_arith(NativeCtx& c, std::string* err) {
    const bool ok = c.ai64(0, &c.v0) && c.ai64(1, &c.v1);
    if (!ok) {
        *err = "native: bad operands: " + c.ins.name;
        return false;
    }
    if (c.fn == "time/before" || c.fn == "time/duration/before")
        return c.wbool(c.v0 < c.v1);
    if (c.fn == "time/after" || c.fn == "time/duration/after")
        return c.wbool(c.v0 > c.v1);
    if (c.fn == "time/add")
        return c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "time", c.v0 + c.v1);
    return c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "duration",
                                c.v0 - c.v1);
}

bool h_duration_scale(NativeCtx& c, std::string*) {
    const bool ok = c.ai64(0, &c.v0);
    std::int64_t mul = 1;
    if (c.fn == "time/duration/millis") mul = 1000000;
    else if (c.fn == "time/duration/seconds") mul = 1000000000;
    else if (c.fn == "time/duration/minutes") mul = 60000000000LL;
    else if (c.fn == "time/duration/hours") mul = 3600000000000LL;
    return ok &&
           c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "duration",
                                c.v0 * mul);
}

bool h_duration_as(NativeCtx& c, std::string*) {
    const bool ok = c.ai64(0, &c.v0);
    std::int64_t div = 1;
    if (c.fn.find("millis") != std::string::npos) div = 1000000;
    else if (c.fn.find("seconds") != std::string::npos) div = 1000000000;
    else if (c.fn.find("minutes") != std::string::npos) div = 60000000000LL;
    else if (c.fn.find("hours") != std::string::npos) div = 3600000000000LL;
    Scalar outv{};
    outv.k = Scalar::K::Int;
    outv.width = 8;
    outv.i = c.v0 / div;
    return ok && c.vm.slot_write_scalar(c.f, c.ins.wr[0], outv, 8);
}

// ── random ────────────────────────────────────────────────────────────────
bool h_random(NativeCtx& c, std::string* err) {
    std::uint64_t r = 0;
#if defined(__APPLE__)
    arc4random_buf(&r, sizeof r);
#else
    if (getrandom(&r, sizeof r, 0) != (ssize_t)sizeof r) {
        *err = "native: getrandom failed";
        return false;
    }
#endif
    bool ok = true;
    if (c.fn == "random/intn") {
        ok = c.ai64(0, &c.v0);
        const std::uint64_t m = (std::uint64_t)c.v0;
        r = m == 0 ? 0 : r % m;
        ok = ok && c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "uint64",
                                        (std::int64_t)r);
    } else if (c.fn == "random/int63") {
        ok = c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "int64",
                                  (std::int64_t)(r >> 1));
    } else {
        ok = c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "uint64",
                                  (std::int64_t)r);
    }
    return ok;
}

// ── compactarray ──────────────────────────────────────────────────────────
bool h_array_new(NativeCtx& c, std::string* err) {
    // 数组字面量 `[v...]`：读参[0..n-1]=元素，写槽描述符 `a <langtype>`
    // 提供 elem kind（`[]int64`/`[N]T`），打包为 [n]elem compact XValue。
    std::string lt = c.write_slot_lt();
    // 动态成员写槽（`[v...] -> /tmp/mi·*j`）描述符无类型 → fallback 第一元素
    // 的 langtype（对齐 C kvlangDeclElemKind fallback kvlangXvalueKind(in[0])）。
    if (lt.empty() && c.ins.nrd >= 1) {
        Wire ew{};
        Scalar es{};
        if (c.vm.read_reg(c.f, c.ins.rd[0], &ew) && v_scalar(ew, &es)) {
            if (es.k == Scalar::K::Float) lt = "float64";
            else if (es.k == Scalar::K::Uint) lt = "uint64";
            else if (es.k == Scalar::K::Bool) lt = "bool";
            else lt = "int64";
        }
    }
    if (!lt.empty() && lt[0] == '[') {
        const std::size_t close = lt.find(']');
        if (close != std::string::npos)
            lt = lt.substr(close + 1);
    }
    const std::uint32_t esz = c.elem_size(lt);
    if (esz == 0 || c.ins.nrd < 1) {
        *err = "array: bad type/elements " + c.ins.name;
        return false;
    }
    const std::int32_t nd = static_cast<std::int32_t>(c.ins.nrd);
    std::vector<std::uint8_t> raw(static_cast<std::uint64_t>(nd) * esz, 0);
    for (int i = 0; i < c.ins.nrd; i++) {
        Wire ew{};
        Scalar es{};
        if (!c.vm.read_reg(c.f, c.ins.rd[i], &ew) || !v_scalar(ew, &es)) {
            *err = "array: bad element " + std::to_string(i);
            return false;
        }
        const std::uint64_t off = (std::uint64_t)i * esz;
        if (esz == 8) {
            if (lt.rfind("float", 0) == 0)
                std::memcpy(raw.data() + off, &es.f, 8);
            else {
                std::uint64_t u = es.k == Scalar::K::Int ? (std::uint64_t)es.i
                                                         : es.u;
                std::memcpy(raw.data() + off, &u, 8);
            }
        } else if (esz == 4) {
            if (lt.rfind("float", 0) == 0) {
                float f2 = (float)es.f;
                std::memcpy(raw.data() + off, &f2, 4);
            } else {
                std::uint32_t u = es.k == Scalar::K::Int
                                      ? (std::uint32_t)es.i
                                      : (std::uint32_t)es.u;
                std::memcpy(raw.data() + off, &u, 4);
            }
        } else if (esz == 2) {
            std::uint16_t u = (std::uint16_t)(es.k == Scalar::K::Int
                                                  ? es.i
                                                  : (std::int64_t)es.u);
            std::memcpy(raw.data() + off, &u, 2);
        } else {
            std::uint8_t b =
                es.k == Scalar::K::Int ? (std::uint8_t)es.i : (std::uint8_t)es.u;
            std::memset(raw.data() + off, b, 1);
        }
    }
    const std::int32_t adims[1] = {nd};
    std::uint8_t* arr = nullptr;
    std::uint32_t al = 0;
    const bool ok = kvspaceXvalueEncode(
                        lt.c_str(), raw.data(),
                        static_cast<std::uint32_t>(raw.size()), adims, 1,
                        &arr, &al) == 0 &&
                    arr && c.vm.write_reg(c.f, c.ins.wr[0], arr, al);
    std::free(arr);
    return ok;
}

bool h_array_fill(NativeCtx& c, std::string* err) {
    // array·fill(langtype[, value])：按定长数组类型一次成型 compact XValue。
    Wire lw{};
    std::string lt;
    if (c.ins.nrd < 1 || !c.vm.read_reg(c.f, c.ins.rd[0], &lw) ||
        !wire_text(lw.d, lw.n, &lt)) {
        *err = "array·fill: bad langtype";
        return false;
    }
    std::int32_t dims[8];
    std::uint32_t ndim = 0;
    lt = c.parse_dims(lt, dims, &ndim);
    const std::uint32_t esz = c.elem_size(lt);
    if (esz == 0 || ndim == 0) {
        *err = "array·fill: bad type " + lt;
        return false;
    }
    std::uint64_t total = 1;
    for (std::uint32_t i = 0; i < ndim; i++)
        total *= static_cast<std::uint64_t>(dims[i]);
    total *= esz;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(total), 0);
    // value 缺省 0；第二读参为填充值。
    if (c.ins.nrd >= 2) {
        Wire vw{};
        Scalar vs{};
        if (c.vm.read_reg(c.f, c.ins.rd[1], &vw) && v_scalar(vw, &vs)) {
            for (std::uint64_t i = 0; i < total / esz; i++) {
                const std::uint64_t off = i * esz;
                if (lt.rfind("float64", 0) == 0)
                    std::memcpy(raw.data() + off, &vs.f, esz);
                else if (lt.rfind("float32", 0) == 0) {
                    float f2 = (float)vs.f;
                    std::memcpy(raw.data() + off, &f2, esz);
                } else if (lt.rfind("int64", 0) == 0 ||
                           lt.rfind("uint64", 0) == 0) {
                    std::uint64_t u = vs.k == Scalar::K::Int
                                          ? (std::uint64_t)vs.i
                                          : vs.u;
                    std::memcpy(raw.data() + off, &u, esz);
                } else if (lt.rfind("int32", 0) == 0 ||
                           lt.rfind("uint32", 0) == 0) {
                    std::uint32_t u32 =
                        (std::uint32_t)(vs.k == Scalar::K::Int
                                            ? vs.i
                                            : (std::int64_t)vs.u);
                    std::memcpy(raw.data() + off, &u32, esz);
                } else {
                    std::uint8_t b = vs.k == Scalar::K::Int
                                         ? (std::uint8_t)vs.i
                                         : (std::uint8_t)vs.u;
                    std::memset(raw.data() + off, b, esz);
                }
            }
        }
    }
    std::uint8_t* arr = nullptr;
    std::uint32_t al = 0;
    const bool ok = kvspaceXvalueEncode(
                        lt.c_str(), raw.data(),
                        static_cast<std::uint32_t>(raw.size()), dims, ndim,
                        &arr, &al) == 0 &&
                    arr && c.vm.write_reg(c.f, c.ins.wr[0], arr, al);
    std::free(arr);
    return ok;
}

bool h_array_copy(NativeCtx& c, std::string* err) {
    // compactarray·copy(a) -> b：专职创建同形副本（值语义变换的显式第一步）。
    FReg& cr = c.f.regs[c.ins.rd[0]];
    std::uint8_t* cb = nullptr;
    std::uint64_t ccap = 0;
    std::string clt;
    if (c.vm.reg_body(c.f, c.ins.rd[0], &cb, &ccap) && cb &&
        cr.view.head && cr.view.headlen > 18) {
        const char* hp = reinterpret_cast<const char*>(cr.view.head + 18);
        const std::size_t hcap = cr.view.headlen - 18;
        const std::size_t hn = ::strnlen(hp, hcap);
        clt.assign(hp, hn);
    }
    if (clt.empty() || !cb) {
        *err = "compactarray·copy: bad array";
        return false;
    }
    std::int32_t cdims[8] = {};
    std::uint32_t cndim = 0;
    const std::string ck = c.parse_dims(clt, cdims, &cndim);
    std::uint8_t* nb = nullptr;
    std::uint32_t nl = 0;
    const bool ok = kvspaceXvalueEncode(
                        ck.c_str(), cb, static_cast<std::uint32_t>(ccap),
                        cdims, static_cast<std::int32_t>(cndim), &nb, &nl) ==
                        0 &&
                    nb && c.vm.write_reg(c.f, c.ins.wr[0], nb, nl);
    std::free(nb);
    return ok;
}

// 散 key 容器成员枚举删除（对齐 C clear_members）。
void clear_members(void* h, const std::string& base, char* errbuf,
                   std::size_t errcap) {
    const std::string mb = base + "\xC2\xB7";
    std::int32_t pcnt = 0;
    if (kvspaceListLen(h, mb.c_str(), 0, 0, &pcnt) == 0 && pcnt > 0) {
        std::vector<std::string> pk;
        for (std::int32_t i = 0; i < pcnt; i++) {
            std::uint8_t name[512];
            std::uint32_t nn = 0;
            if (kvspaceListAt(h, mb.c_str(), 0, 0, i, name, sizeof name,
                              &nn) != 0)
                break;
            pk.push_back(mb + std::string(reinterpret_cast<const char*>(name),
                                          nn));
        }
        std::vector<const char*> pc;
        for (const std::string& k : pk)
            pc.push_back(k.c_str());
        if (!pc.empty())
            kvspaceDel(h, pc.data(), static_cast<std::uint32_t>(pc.size()),
                       errbuf, errcap);
    }
}

bool h_scatter_new(NativeCtx& c, std::string*) {
    // 空散 key 容器：写 marker（写槽描述符的 langtype，缺省 map）+ 建成员目录。
    std::string lt = c.write_slot_lt();
    if (lt.empty())
        lt = "map";
    std::uint8_t* mark = nullptr;
    std::uint64_t ml = 0;
    bool ok = kvspaceXhNewShort(lt.c_str(), nullptr, 0, &mark, &ml) == 0 &&
              mark && c.vm.write_reg(c.f, c.ins.wr[0], mark,
                                     static_cast<std::uint32_t>(ml));
    std::free(mark);
    if (ok) {
        char e[192] = {};
        const std::string dir = c.f.regs[c.ins.wr[0]].key + "/";
        ok = kvspaceMkindex(c.f.h, dir.c_str(), 0, e, sizeof e) == 0;
    }
    return ok;
}

bool h_scatter_len(NativeCtx& c, std::string* err) {
    // 成员数（memindex 权威）：扫 `base·` 子键空间。
    FReg& lr = c.f.regs[c.ins.rd[0]];
    if (!c.vm.ensure_reg_key(c.f, lr)) {
        *err = "scatterarray·len: bad container";
        return false;
    }
    std::int32_t cnt = 0;
    if (kvspaceListLen(c.f.h, (lr.key + "\xC2\xB7").c_str(), 0, 0, &cnt) != 0)
        cnt = 0;
    return c.vm.slot_write_i64(c.f, c.ins.wr[0], cnt);
}

bool h_scatter_at(NativeCtx& c, std::string* err) {
    // 坐标段成员读：`base·[i]` 取 XValue 写读参（`b·[i]` 降级目标）。
    FReg& ar = c.f.regs[c.ins.rd[0]];
    std::int64_t ai = 0;
    if (c.ins.nrd < 2 || !c.vm.ensure_reg_key(c.f, ar) ||
        !c.vm.read_i64(c.f, c.ins.rd[1], &ai) || ai < 0) {
        *err = "scatterarray·at: bad index";
        return false;
    }
    const std::string mk =
        ar.key + "\xC2\xB7[" + std::to_string(ai) + "]";
    const std::uint8_t* md = nullptr;
    std::uint32_t mn = 0;
    if (!read_wire(c.f.h, mk, &md, &mn) || !md || !mn) {
        *err = "scatterarray·at: index out of range";
        return false;
    }
    return c.vm.write_reg(c.f, c.ins.wr[0], md, mn);
}

bool h_scatter_set(NativeCtx& c, std::string* err) {
    // 坐标段成员写：`base·[i] = v`（只写既有成员，不创建——操作型铁律）。
    FReg& sw = c.f.regs[c.ins.wr[0]];
    std::int64_t si = 0;
    if (c.ins.nrd < 3 || !c.vm.ensure_reg_key(c.f, sw) ||
        !c.vm.read_i64(c.f, c.ins.rd[1], &si) || si < 0) {
        *err = "scatterarray·set: bad index";
        return false;
    }
    Wire sv{};
    if (!c.vm.read_reg(c.f, c.ins.rd[2], &sv)) {
        *err = "scatterarray·set: bad value";
        return false;
    }
    const std::string mk = sw.key + "\xC2\xB7[" + std::to_string(si) + "]";
    char errbuf[256] = {};
    const bool ok = kvspaceSetValue(c.f.h, mk.c_str(), sv.d, sv.n, 0, 0,
                                    errbuf, sizeof errbuf) == 0;
    if (!ok && errbuf[0])
        *err = std::string(c.ins.name) + ": " + errbuf;
    return ok;
}

bool h_scatter_newcompact(NativeCtx& c, std::string* err) {
    // 源散 key 容器（读参 0 = 容器值 marker，key 指向容器槽）。
    FReg& cr = c.f.regs[c.ins.rd[0]];
    if (!c.vm.ensure_reg_key(c.f, cr)) {
        *err = "array·compact: bad src";
        return false;
    }
    const std::string cb = cr.key;
    std::int32_t cnt = 0;
    if (kvspaceListLen(c.f.h, (cb + "\xC2\xB7").c_str(), 0, 0, &cnt) != 0 ||
        cnt <= 0) {
        // 空散列：写空 compact（ndim=1 dims=[0]）。
        const std::int32_t z[1] = {0};
        std::uint8_t* zb = nullptr;
        std::uint32_t zl = 0;
        const bool ok = kvspaceXvalueEncode(
                            "int64", nullptr, 0, z, 1, &zb, &zl) == 0 &&
                        zb && c.vm.write_reg(c.f, c.ins.wr[0], zb, zl);
        std::free(zb);
        return ok;
    }
    // 读元素 [0..cnt-1]，取首元素 kind 打包。
    std::vector<std::uint8_t> raw;
    std::string ekind;
    bool first = true;
    for (std::int32_t i = 0; i < cnt; i++) {
        const std::string mk = cb + "\xC2\xB7[" + std::to_string(i) + "]";
        const std::uint8_t* md = nullptr;
        std::uint32_t mn = 0;
        if (!read_wire(c.f.h, mk, &md, &mn) || !md || !mn)
            break;
        if (first) {
            std::string lt;
            if (v_langtype(Wire{md, mn}, &lt)) {
                ekind = lt;
                if (!ekind.empty() && ekind[0] == '[') {
                    const std::size_t cl = ekind.find(']');
                    if (cl != std::string::npos)
                        ekind = ekind.substr(cl + 1);
                }
            }
            first = false;
        }
        kvspaceXh eh{};
        if (kvspaceXhDecode(md, mn, &eh) != 0 || !eh.body)
            break;
        raw.insert(raw.end(), eh.body, eh.body + eh.content_len);
    }
    const std::uint32_t esz = c.elem_size(ekind);
    if (esz == 0) {
        *err = "array·compact: bad elem kind " + ekind;
        return false;
    }
    const std::int32_t nd = cnt;
    std::uint8_t* nb = nullptr;
    std::uint32_t nl = 0;
    const bool ok = kvspaceXvalueEncode(
                        ekind.c_str(), raw.data(),
                        static_cast<std::uint32_t>(raw.size()), &nd, 1, &nb,
                        &nl) == 0 &&
                    nb && c.vm.write_reg(c.f, c.ins.wr[0], nb, nl);
    std::free(nb);
    return ok;
}

bool h_scatter_newscatter(NativeCtx& c, std::string* err) {
    // 读参[0]=compact 数组；写参=散 key 容器。清旧成员 + 写 marker + 逐元素。
    FReg& sr = c.f.regs[c.ins.rd[0]];
    std::uint8_t* sb = nullptr;
    std::uint64_t scap = 0;
    std::string slt;
    std::uint32_t sesz = 0;
    if (c.vm.reg_body(c.f, c.ins.rd[0], &sb, &scap) && sb &&
        sr.view.head && sr.view.headlen > 18) {
        const char* hp = reinterpret_cast<const char*>(sr.view.head + 18);
        const std::size_t hcap = sr.view.headlen - 18;
        const std::size_t hn = ::strnlen(hp, hcap);
        slt.assign(hp, hn);
        if (!slt.empty() && slt[0] == '[') {
            const std::size_t cl = slt.find(']');
            if (cl != std::string::npos)
                slt = slt.substr(cl + 1);
        }
        sesz = c.elem_size(slt);
    }
    if (sesz == 0 || !sb) {
        *err = "array·scatter: bad array";
        return false;
    }
    FReg& wr0 = c.f.regs[c.ins.wr[0]];
    if (!c.vm.ensure_reg_key(c.f, wr0)) {
        *err = "array·scatter: bad dst";
        return false;
    }
    const std::string db = wr0.key;
    char errbuf[256] = {};
    clear_members(c.f.h, db, errbuf, sizeof errbuf);
    const std::string mklt = "[int64]\xC2\xB7" + slt;
    std::uint8_t* mark = nullptr;
    std::uint64_t ml = 0;
    bool ok = kvspaceXhNewShort(mklt.c_str(), nullptr, 0, &mark, &ml) == 0 &&
              mark && kvspaceSetValue(c.f.h, db.c_str(), mark,
                                      static_cast<std::uint32_t>(ml), 0, 0,
                                      errbuf, sizeof errbuf) == 0;
    std::free(mark);
    if (!ok) {
        *err = "array·scatter: marker failed";
        return false;
    }
    const std::int64_t n = static_cast<std::int64_t>(scap / sesz);
    for (std::int64_t i = 0; i < n; i++) {
        const std::string mk = db + "\xC2\xB7[" + std::to_string(i) + "]";
        std::uint8_t* eb = nullptr;
        std::uint32_t el = 0;
        const std::int32_t ed[1] = {1};
        ok = kvspaceXvalueEncode(
                 slt.c_str(), sb + i * sesz,
                 static_cast<std::uint32_t>(sesz), ed, 1, &eb, &el) == 0 &&
             eb && kvspaceSetValue(c.f.h, mk.c_str(), eb, el, 0, 0, errbuf,
                                   sizeof errbuf) == 0;
        std::free(eb);
        if (!ok) {
            *err = "array·scatter: elem failed";
            return false;
        }
    }
    return true;
}

bool h_scatter_append(NativeCtx& c, std::string* err) {
    // 读参[0]=散列容器，读参[1]=元素；写参=容器。坐标=max+1。
    FReg& bw = c.f.regs[c.ins.wr[0]];
    if (!c.vm.ensure_reg_key(c.f, bw)) {
        *err = "scatterarray·append: bad write";
        return false;
    }
    const std::string bb = bw.key;
    // compact 源检测：读参 0 的 langtype 无 `·`（map 形态标志）且带
    // `[`（compact 数组）→ TypeError。
    {
        Wire bw0{};
        std::string blt;
        if (c.vm.read_reg(c.f, c.ins.rd[0], &bw0) &&
            v_langtype(Wire{bw0.d, bw0.n}, &blt) &&
            blt.find("\xC2\xB7") == std::string::npos &&
            blt.find('[') != std::string::npos) {
            *err = "TypeError: scatterarray·append requires a scattered "
                   "container; compact source must be scattered first "
                   "(scatterarray·newscatter)";
            return false;
        }
    }
    // 坐标 = 末元素 max+1（memindex 升序）。
    std::int32_t cnt = 0;
    kvspaceListLen(c.f.h, (bb + "\xC2\xB7").c_str(), 0, 0, &cnt);
    std::int64_t next = 0;
    if (cnt > 0) {
        std::uint8_t name[512];
        std::uint32_t nn = 0;
        if (kvspaceListAt(c.f.h, (bb + "\xC2\xB7").c_str(), 0, 0, cnt - 1,
                          name, sizeof name, &nn) == 0 &&
            nn > 2 && name[0] == '[') {
            char* end = nullptr;
            const long long idx =
                std::strtoll(reinterpret_cast<const char*>(name + 1), &end, 10);
            if (end && *end == ']')
                next = static_cast<std::int64_t>(idx) + 1;
        }
    }
    Wire ev{};
    if (c.ins.nrd < 2 || !c.vm.read_reg(c.f, c.ins.rd[1], &ev)) {
        *err = "array·append: bad element";
        return false;
    }
    const std::string mk = bb + "\xC2\xB7[" + std::to_string(next) + "]";
    char errbuf[256] = {};
    const bool ok = kvspaceSetValue(c.f.h, mk.c_str(), ev.d, ev.n, 0, 0,
                                    errbuf, sizeof errbuf) == 0;
    if (!ok) {
        *err = "array·append: set failed";
        return false;
    }
    return true;
}

bool h_scatter_slice(NativeCtx& c, std::string* err) {
    // scatterarray·slice(a, lo, hi) -> a：散列成员重排（不创建新 compact）。
    std::int64_t lo = 0, hi = 0;
    if (c.ins.nrd < 3 || !c.vm.read_i64(c.f, c.ins.rd[1], &lo) ||
        !c.vm.read_i64(c.f, c.ins.rd[2], &hi) || lo < 0 || hi < lo) {
        *err = "scatterarray·slice: bad bounds";
        return false;
    }
    FReg& sw = c.f.regs[c.ins.wr[0]];
    if (!c.vm.ensure_reg_key(c.f, sw)) {
        *err = "scatterarray·slice: bad write";
        return false;
    }
    const std::string bb = sw.key;
    const std::string dir = bb + "\xC2\xB7";
    std::int32_t cnt = 0;
    if (kvspaceListLen(c.f.h, dir.c_str(), 0, 0, &cnt) != 0)
        cnt = 0;
    std::vector<std::string> names;
    if (cnt > 0) {
        for (std::int32_t i = 0; i < cnt; i++) {
            std::uint8_t name[512];
            std::uint32_t nn = 0;
            if (kvspaceListAt(c.f.h, dir.c_str(), 0, 0, i, name, sizeof name,
                              &nn) != 0)
                break;
            names.push_back(std::string(reinterpret_cast<const char*>(name),
                                        nn));
        }
    }
    const std::int64_t alen = static_cast<std::int64_t>(names.size());
    if (lo > alen || hi > alen) {
        *err = "scatterarray·slice: index out of range";
        return false;
    }
    for (std::int64_t r = lo; r < hi; r++) {
        const std::string sk = dir + names[(std::size_t)r];
        const std::uint8_t* vd = nullptr;
        std::uint32_t vn = 0;
        if (!read_wire(c.f.h, sk, &vd, &vn) || !vd || !vn) {
            *err = "scatterarray·slice: missing member";
            return false;
        }
        const std::string dk = bb + "\xC2\xB7[" + std::to_string(r - lo) + "]";
        char errbuf[256] = {};
        if (kvspaceSetValue(c.f.h, dk.c_str(), vd, vn, 0, 0, errbuf,
                            sizeof errbuf) != 0) {
            *err = "scatterarray·slice: set failed";
            return false;
        }
    }
    // 删源成员中坐标 >= hi-lo 的（对齐 C rank_path 的 cut：洞不是元素）。
    char errbuf[256] = {};
    for (const std::string& nm : names) {
        const char* p = nm.c_str();
        if (nm.size() >= 2 && p[0] == '[' && p[nm.size() - 1] == ']') {
            char* end = nullptr;
            const long long coord = std::strtoll(p + 1, &end, 10);
            if (end && *end == ']' && coord >= (hi - lo)) {
                const std::string dk =
                    bb + "\xC2\xB7[" + std::to_string(coord) + "]";
                const char* dk1 = dk.c_str();
                kvspaceDel(c.f.h, &dk1, 1, errbuf, sizeof errbuf);
            }
        }
    }
    return true;
}

bool h_array_newslice(NativeCtx& c, std::string* err) {
    // compactarray·newslice(a, lo, hi) -> b：compact 裁剪造新 compact。
    std::int64_t lo = 0, hi = 0;
    if (c.ins.nrd < 3 || !c.vm.read_i64(c.f, c.ins.rd[1], &lo) ||
        !c.vm.read_i64(c.f, c.ins.rd[2], &hi)) {
        *err = "compactarray·newslice: bad bounds";
        return false;
    }
    FReg& sr = c.f.regs[c.ins.rd[0]];
    std::uint8_t* sb = nullptr;
    std::uint64_t scap = 0;
    std::string slt;
    std::uint32_t sesz = 0;
    if (c.vm.reg_body(c.f, c.ins.rd[0], &sb, &scap) && sb &&
        sr.view.head && sr.view.headlen > 18) {
        const char* hp = reinterpret_cast<const char*>(sr.view.head + 18);
        const std::size_t hcap = sr.view.headlen - 18;
        const std::size_t hn = ::strnlen(hp, hcap);
        slt.assign(hp, hn);
        if (!slt.empty() && slt[0] == '[') {
            const std::size_t cl = slt.find(']');
            if (cl != std::string::npos)
                slt = slt.substr(cl + 1);
        }
        sesz = c.elem_size(slt);
    }
    if (sesz == 0 || !sb) {
        *err = "compactarray·newslice: bad array";
        return false;
    }
    const std::int64_t alen = static_cast<std::int64_t>(scap / sesz);
    if (lo < 0 || hi < lo || hi > alen) {
        *err = "compactarray·newslice: index out of range";
        return false;
    }
    const std::int32_t nd = static_cast<std::int32_t>(hi - lo);
    std::uint8_t* nb = nullptr;
    std::uint32_t nl = 0;
    const bool ok = kvspaceXvalueEncode(
                        slt.c_str(), sb + lo * sesz,
                        static_cast<std::uint32_t>((hi - lo) * sesz), &nd, 1,
                        &nb, &nl) == 0 &&
                    nb && c.vm.write_reg(c.f, c.ins.wr[0], nb, nl);
    std::free(nb);
    return ok;
}

// ── string ────────────────────────────────────────────────────────────────
bool h_string_char(NativeCtx& c, std::string* err) {
    std::int64_t idx = 0;
    if (c.ins.nrd < 2 || !c.vm.read_i64(c.f, c.ins.rd[1], &idx)) {
        *err = "string·char: bad input";
        return false;
    }
    std::uint8_t* sb = nullptr;
    std::uint64_t sc = 0;
    if (!c.vm.reg_body(c.f, c.ins.rd[0], &sb, &sc) || !sb) {
        *err = "string·char: bad string";
        return false;
    }
    FReg& sr = c.f.regs[c.ins.rd[0]];
    const std::uint8_t* hd = sr.view.head;
    const std::size_t hcap =
        hd ? static_cast<std::size_t>(sr.view.headlen > 18
                                         ? sr.view.headlen - 18
                                         : 0)
           : 0;
    const std::size_t hlen =
        hcap ? ::strnlen(reinterpret_cast<const char*>(hd + 18), hcap) : 0;
    const bin::LangType st = c.vm.bin.langtype_of(
        reinterpret_cast<const char*>(hd + 18), hlen);
    std::uint32_t cp = 0;
    const bool utf32 = st == bin::LangType::CharUtf32;
    const std::uint64_t blen = sc;
    if (utf32) {
        const std::uint32_t n = static_cast<std::uint32_t>(blen / 4);
        if (idx >= 0 && (std::uint64_t)idx < n)
            std::memcpy(&cp, sb + (std::uint64_t)idx * 4, 4);
        else {
            *err = "string·char: index out of bounds";
            return false;
        }
    } else {
        const std::uint8_t* b = sb;
        std::int64_t rune_i = 0;
        for (std::uint64_t i = 0; i < blen && rune_i <= idx;) {
            const std::uint8_t c0 = b[i];
            std::uint32_t r = 0, take = 1;
            if (c0 < 0x80) { r = c0; }
            else if ((c0 >> 5) == 0x6) { r = c0 & 0x1F; take = 2; }
            else if ((c0 >> 4) == 0xE) { r = c0 & 0x0F; take = 3; }
            else if ((c0 >> 3) == 0x1E) { r = c0 & 0x07; take = 4; }
            for (std::uint32_t k = 1; k < take && i + k < blen; k++)
                r = (r << 6) | (b[i + k] & 0x3F);
            if (rune_i == idx) { cp = r; break; }
            rune_i++;
            i += take;
        }
        if (rune_i < idx) {
            *err = "string·char: index out of bounds";
            return false;
        }
    }
    const std::uint32_t u32 = cp;
    std::uint8_t* cb = nullptr;
    std::uint32_t cl = 0;
    const std::int32_t cdims[1] = {1};
    const bool ok = kvspaceXvalueEncode(
                        "char/utf32", reinterpret_cast<const std::uint8_t*>(&u32),
                        4, cdims, 1, &cb, &cl) == 0 &&
                    cb && c.vm.write_reg(c.f, c.ins.wr[0], cb, cl);
    std::free(cb);
    return ok;
}

bool h_string_len(NativeCtx& c, std::string* err) {
    Wire sw{};
    if (c.ins.nrd < 1 || !c.vm.read_reg(c.f, c.ins.rd[0], &sw)) {
        *err = "string·len: bad input";
        return false;
    }
    kvspaceXh sh{};
    std::int64_t len = 0;
    if (kvspaceXhDecode(sw.d, sw.n, &sh) == 0 && sh.body) {
        std::string sl(reinterpret_cast<const char*>(sh.langtype),
                       sh.langtype_len);
        if (sl.find("utf32") != std::string::npos) {
            len = static_cast<std::int64_t>(sh.content_len / 4);
        } else if (sl.find("ascii") != std::string::npos) {
            len = static_cast<std::int64_t>(sh.content_len);
        } else {
            const std::uint8_t* b = sh.body;
            for (std::uint64_t i = 0; i < sh.content_len; i++)
                if ((b[i] & 0xC0) != 0x80)
                    len++;
        }
    }
    return c.vm.slot_write_i64(c.f, c.ins.wr[0], len);
}

bool h_string_ord(NativeCtx& c, std::string* err) {
    Wire sw{};
    if (c.ins.nrd < 1 || !c.vm.read_reg(c.f, c.ins.rd[0], &sw)) {
        *err = "string·ord: bad input";
        return false;
    }
    kvspaceXh sh{};
    std::int64_t cp = -1;
    if (kvspaceXhDecode(sw.d, sw.n, &sh) == 0 && sh.body && sh.content_len) {
        std::string sl(reinterpret_cast<const char*>(sh.langtype),
                       sh.langtype_len);
        const std::uint8_t* b = sh.body;
        if (sl.find("utf32") != std::string::npos) {
            std::uint32_t u = 0;
            std::memcpy(&u, b, 4);
            cp = u;
        } else {
            std::uint32_t r = 0, take = 1;
            const std::uint8_t c0 = b[0];
            if (c0 < 0x80) { r = c0; }
            else if ((c0 >> 5) == 0x6) { r = c0 & 0x1F; take = 2; }
            else if ((c0 >> 4) == 0xE) { r = c0 & 0x0F; take = 3; }
            else if ((c0 >> 3) == 0x1E) { r = c0 & 0x07; take = 4; }
            for (std::uint32_t k = 1; k < take && k < sh.content_len; k++)
                r = (r << 6) | (b[k] & 0x3F);
            cp = r;
        }
    }
    return c.vm.slot_write_i64(c.f, c.ins.wr[0], cp);
}

// ── string 家族共用 ────────────────────────────────────────────────────────
// UTF-8 文本 → 码点数组（无效序列按 0xFFFD，对齐 C utf8_decode_next）。
std::vector<std::uint32_t> utf8_to_runes(const std::string& s) {
    std::vector<std::uint32_t> r;
    r.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const std::uint32_t c0 = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c0, take = 1;
        if (c0 < 0x80) cp = c0;
        else if ((c0 >> 5) == 0x6) { cp = c0 & 0x1F; take = 2; }
        else if ((c0 >> 4) == 0xE) { cp = c0 & 0x0F; take = 3; }
        else if ((c0 >> 3) == 0x1E) { cp = c0 & 0x07; take = 4; }
        else { cp = 0xFFFD; take = 1; }
        if (i + take > s.size()) { cp = 0xFFFD; take = 1; }
        for (std::size_t k = 1; k < take && i + k < s.size(); k++)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        r.push_back(cp);
        i += take;
    }
    return r;
}

// 码点数组 → char/utf32 XValue → 写槽（字符串 ops 的统一输出）。
bool write_runes(NativeCtx& c, const std::vector<std::uint32_t>& r) {
    const std::uint32_t n = static_cast<std::uint32_t>(r.size());
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(n) * 4, 0);
    for (std::uint32_t i = 0; i < n; i++)
        std::memcpy(raw.data() + static_cast<std::size_t>(i) * 4, &r[i], 4);
    const std::int32_t dims[1] = {static_cast<std::int32_t>(n)};
    std::uint8_t* out = nullptr;
    std::uint32_t ol = 0;
    const bool ok = kvspaceXvalueEncode(
                        "char/utf32", raw.data(),
                        static_cast<std::uint32_t>(raw.size()), dims, 1, &out,
                        &ol) == 0 &&
                    out && c.vm.write_reg(c.f, c.ins.wr[0], out, ol);
    std::free(out);
    return ok;
}

// 读参必须是 char 族（剥 dims 前缀后 kind 为 char/*；对齐 C 的 char 判定）。
bool is_char_wire(const Wire& w) {
    std::string lt;
    if (!v_langtype(w, &lt))
        return false;
    const std::size_t close = lt.find(']');
    if (!lt.empty() && lt[0] == '[' && close != std::string::npos)
        lt = lt.substr(close + 1);
    return lt.rfind("char/", 0) == 0;
}

bool h_string_cmp(NativeCtx& c, std::string* err) {
    std::string a, b;
    if (c.ins.nrd < 2 || !c.atext(0, &a) || !c.atext(1, &b)) {
        *err = "string·cmp: bad input";
        return false;
    }
    const std::int64_t r = a < b ? -1 : a > b ? 1 : 0;
    return c.vm.slot_write_i64(c.f, c.ins.wr[0], r);
}

bool h_string_slice(NativeCtx& c, std::string* err) {
    std::string s;
    std::int64_t lo = 0, hi = 0;
    if (c.ins.nrd < 3 || !c.atext(0, &s) ||
        !c.vm.read_i64(c.f, c.ins.rd[1], &lo) ||
        !c.vm.read_i64(c.f, c.ins.rd[2], &hi)) {
        *err = "string·slice: bad input";
        return false;
    }
    const std::vector<std::uint32_t> r = utf8_to_runes(s);
    if (lo < 0 || hi > static_cast<std::int64_t>(r.size()) || lo > hi) {
        *err = "string·slice: index out of bounds";
        return false;
    }
    return write_runes(c, std::vector<std::uint32_t>(r.begin() + lo, r.begin() + hi));
}

bool h_string_find(NativeCtx& c, std::string* err) {
    std::string hs, nd;
    if (c.ins.nrd < 2 || !c.atext(0, &hs) || !c.atext(1, &nd)) {
        *err = "string·find: bad input";
        return false;
    }
    const std::vector<std::uint32_t> hay = utf8_to_runes(hs);
    const std::vector<std::uint32_t> ndl = utf8_to_runes(nd);
    std::int64_t r = -1;
    if (ndl.empty())
        r = 0;
    else
        for (std::size_t i = 0; i + ndl.size() <= hay.size(); i++) {
            bool m = true;
            for (std::size_t j = 0; j < ndl.size(); j++)
                if (hay[i + j] != ndl[j]) { m = false; break; }
            if (m) { r = static_cast<std::int64_t>(i); break; }
        }
    return c.vm.slot_write_i64(c.f, c.ins.wr[0], r);
}

bool h_string_concat(NativeCtx& c, std::string* err) {
    if (c.ins.nrd < 2 || !is_char_wire(c.in[0]) || !is_char_wire(c.in[1])) {
        *err = "string·concat: requires strings";
        return false;
    }
    std::string a, b;
    if (!c.atext(0, &a) || !c.atext(1, &b)) {
        *err = "string·concat: bad input";
        return false;
    }
    return write_runes(c, utf8_to_runes(a + b));
}

// 对齐 C format_base：base 2..36，非法 base 回退 10；负数带 '-'。
std::string format_base(std::uint64_t u, int base, bool neg) {
    if (base < 2 || base > 36)
        base = 10;
    char tmp[65];
    int n = 0;
    if (u == 0)
        tmp[n++] = '0';
    while (u) {
        const int d = static_cast<int>(u % static_cast<unsigned>(base));
        tmp[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
        u /= static_cast<unsigned>(base);
    }
    std::string out;
    if (neg)
        out.push_back('-');
    for (int i = n - 1; i >= 0; i--)
        out.push_back(tmp[i]);
    return out;
}

bool h_string_formatint(NativeCtx& c, std::string* err) {
    std::int64_t v = 0, base = 10;
    if (c.ins.nrd < 1 || !c.ai64(0, &v)) {
        *err = "string·formatint: bad input";
        return false;
    }
    if (c.ins.nrd >= 2)
        (void)c.ai64(1, &base);
    const std::uint64_t u =
        v < 0 ? static_cast<std::uint64_t>(-(v + 1)) + 1 : static_cast<std::uint64_t>(v);
    return write_runes(c, utf8_to_runes(format_base(u, static_cast<int>(base), v < 0)));
}

bool h_string_formatuint(NativeCtx& c, std::string* err) {
    std::int64_t v = 0, base = 10;
    if (c.ins.nrd < 1 || !c.ai64(0, &v)) {
        *err = "string·formatuint: bad input";
        return false;
    }
    if (c.ins.nrd >= 2)
        (void)c.ai64(1, &base);
    return write_runes(c, utf8_to_runes(
                               format_base(static_cast<std::uint64_t>(v),
                                           static_cast<int>(base), false)));
}

// 整串消费完（无尾随空白/垃圾），否则 ValueError（对齐 C strtoll 三查）。
bool parse_consumed(const std::string& s, char* end, const char* start) {
    return !s.empty() && end != start && *end == '\0';
}

bool h_string_parseint(NativeCtx& c, std::string* err) {
    std::string s;
    std::int64_t base = 10;
    if (c.ins.nrd < 1 || !c.atext(0, &s)) {
        *err = "string·parseint: bad input";
        return false;
    }
    if (c.ins.nrd >= 2)
        (void)c.ai64(1, &base);
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, static_cast<int>(base));
    if (!parse_consumed(s, end, s.c_str())) {
        *err = "ValueError: string·parseint: invalid syntax";
        return false;
    }
    return c.vm.slot_write_i64(c.f, c.ins.wr[0], static_cast<std::int64_t>(v));
}

bool h_string_parseuint(NativeCtx& c, std::string* err) {
    std::string s;
    std::int64_t base = 10;
    if (c.ins.nrd < 1 || !c.atext(0, &s)) {
        *err = "string·parseuint: bad input";
        return false;
    }
    if (c.ins.nrd >= 2)
        (void)c.ai64(1, &base);
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, static_cast<int>(base));
    if (!parse_consumed(s, end, s.c_str())) {
        *err = "ValueError: string·parseuint: invalid syntax";
        return false;
    }
    return c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "uint64",
                                static_cast<std::int64_t>(v));
}

bool h_string_parsefloat(NativeCtx& c, std::string* err) {
    std::string s;
    if (c.ins.nrd < 1 || !c.atext(0, &s)) {
        *err = "string·parsefloat: bad input";
        return false;
    }
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (!parse_consumed(s, end, s.c_str())) {
        *err = "ValueError: string·parsefloat: invalid syntax";
        return false;
    }
    std::uint8_t raw[8];
    std::memcpy(raw, &v, 8);
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    const bool ok = kvspaceXvalueEncode("float64", raw, 8, nullptr, 0, &buf,
                                        &len) == 0 &&
                    buf && c.vm.write_reg(c.f, c.ins.wr[0], buf, len);
    std::free(buf);
    return ok;
}

bool h_string_set(NativeCtx& c, std::string* err) {
    // 对齐 C kvlangBuiltinStringSet：读参[0] 的显示文本写为 char/utf32。
    std::string s;
    if (c.ins.nrd < 1 || !c.atext(0, &s)) {
        *err = "string·set: bad input";
        return false;
    }
    return write_runes(c, utf8_to_runes(s));
}

// ── ndarray 元数据（numel/dim/shape）──────────────────────────────────────
bool h_array_meta(NativeCtx& c, std::string* err) {
    Wire aw{};
    if (c.ins.nrd < 1 || !c.vm.read_reg(c.f, c.ins.rd[0], &aw)) {
        *err = "ndarray: bad array";
        return false;
    }
    kvspaceXh axh{};
    if (kvspaceXhDecode(aw.d, aw.n, &axh) != 0) {
        *err = "ndarray: not a compact array";
        return false;
    }
    std::string alt(reinterpret_cast<const char*>(axh.langtype),
                    axh.langtype_len);
    std::int32_t adims[8];
    std::uint32_t andim = 0;
    const bool is_map = alt.find("\xC2\xB7") != std::string::npos;
    alt = c.parse_dims(alt, adims, &andim);
    bool ok = true;
    if (c.fn == "compactarray/numel") {
        if (is_map) {
            std::string base_key;
            if (c.f.regs[c.ins.rd[0]].key.size() > 5)
                base_key = c.f.regs[c.ins.rd[0]].key;
            std::int32_t cnt = 0;
            ok = !base_key.empty() &&
                 kvspaceListLen(c.f.h, (base_key + "\xC2\xB7").c_str(), 0, 0,
                                &cnt) == 0;
            if (ok)
                ok = c.vm.slot_write_i64(c.f, c.ins.wr[0], cnt);
        } else {
            std::int64_t total = 1;
            for (std::uint32_t i = 0; i < andim; i++)
                total *= adims[i];
            ok = c.vm.slot_write_i64(c.f, c.ins.wr[0], total);
        }
    } else if (c.fn == "compactarray/dim") {
        ok = c.vm.slot_write_i64(c.f, c.ins.wr[0], andim);
    } else {
        std::uint8_t raw[64];
        for (std::uint32_t i = 0; i < andim && i < 8; i++) {
            const std::int64_t d = adims[i];
            for (int j = 0; j < 8; j++)
                raw[i * 8 + j] = (d >> (j * 8)) & 0xFF;
        }
        const std::int32_t sd[1] = {static_cast<std::int32_t>(andim)};
        std::uint8_t* sb = nullptr;
        std::uint32_t sl = 0;
        ok = kvspaceXvalueEncode("int64", raw, andim * 8, sd, 1, &sb, &sl) == 0 &&
             sb && c.vm.write_reg(c.f, c.ins.wr[0], sb, sl);
        std::free(sb);
    }
    return ok;
}

// ── xv（langtype/bodylen/parselangtype/reinterpret）────────────────────────
bool h_xv_meta(NativeCtx& c, std::string* err) {
    FReg& mr = c.f.regs[c.ins.rd[0]];
    std::uint8_t* mb = nullptr;
    std::uint64_t mcap = 0;
    std::string mlt;
    if (mr.fast_i64) {
        mlt = "int64";
        mcap = 8;
    } else if (mr.fast_bool) {
        mlt = "bool";
        mcap = 1;
    } else if (c.vm.reg_body(c.f, c.ins.rd[0], &mb, &mcap) && mb &&
               mr.view.head && mr.view.headlen > 18) {
        const char* hp = reinterpret_cast<const char*>(mr.view.head + 18);
        const std::size_t hcap = mr.view.headlen - 18;
        const std::size_t hn = ::strnlen(hp, hcap);
        mlt.assign(hp, hn);
    }
    bool ok = true;
    if (c.fn == "compactarray/langtype") {
        std::uint8_t* sb = nullptr;
        std::uint32_t sl = 0;
        ok = kvspaceXvalueEncode(
                 "char/utf8", reinterpret_cast<const std::uint8_t*>(mlt.data()),
                 static_cast<std::uint32_t>(mlt.size()), nullptr, 0, &sb, &sl) ==
                 0 &&
             sb && c.vm.write_reg(c.f, c.ins.wr[0], sb, sl);
        std::free(sb);
    } else if (c.fn == "compactarray/bodylen") {
        std::int64_t blen = 0;
        if (mr.fast_i64) blen = 8;
        else if (mr.fast_bool) blen = 1;
        else if (mr.view.head && mr.view.headlen) {
            kvspaceXh bh{};
            if (kvspaceXhDecode(mr.view.head, mr.view.headlen + mr.view.body_cap,
                                &bh) == 0)
                blen = static_cast<std::int64_t>(bh.content_len);
        }
        ok = c.vm.slot_write_i64(c.f, c.ins.wr[0], blen);
    } else if (c.fn == "compactarray/parselangtype") {
        std::string base = mlt;
        if (!base.empty() && base[0] == '[') {
            const std::size_t close = base.find(']');
            if (close != std::string::npos)
                base = base.substr(close + 1);
        }
        if (base.rfind("def ", 0) == 0)
            base = base.substr(4);
        std::uint8_t* sb = nullptr;
        std::uint32_t sl = 0;
        ok = kvspaceXvalueEncode(
                 "char/utf8", reinterpret_cast<const std::uint8_t*>(base.data()),
                 static_cast<std::uint32_t>(base.size()), nullptr, 0, &sb, &sl) ==
                 0 &&
             sb && c.vm.write_reg(c.f, c.ins.wr[0], sb, sl);
        std::free(sb);
        if (ok && c.ins.nwr >= 2) {
            std::int32_t nd = 0;
            if (!mlt.empty() && mlt[0] == '[') {
                const std::size_t close = mlt.find(']');
                if (close != std::string::npos) {
                    const std::string seg = mlt.substr(1, close - 1);
                    std::size_t i = 0;
                    while (i <= seg.size() && nd < 8) {
                        const std::size_t comma = seg.find(',', i);
                        nd++;
                        if (comma == std::string::npos) break;
                        i = comma + 1;
                    }
                }
            }
            ok = c.vm.slot_write_i64(c.f, c.ins.wr[1], nd);
        }
    } else {
        // xv·reinterpret：读参[1]=新 kindexpr 文本；body 原样，dims 由传入串解析。
        Wire lw{};
        std::string nlt;
        if (c.ins.nrd < 2 || !c.vm.read_reg(c.f, c.ins.rd[1], &lw) ||
            !wire_text(lw.d, lw.n, &nlt) || nlt.empty()) {
            *err = "xv·reinterpret: bad langtype";
            return false;
        }
        std::int32_t ndims[8] = {};
        int ndim = 0;
        std::string kk = nlt;
        if (!kk.empty() && kk[0] == '[') {
            const std::size_t close = kk.find(']');
            if (close != std::string::npos) {
                const std::string seg = kk.substr(1, close - 1);
                if (seg.empty()) {
                    // `[]kind`：动态一维，按 body 字节补。
                    kk = kk.substr(close + 1);
                    std::uint32_t esz = 0;
                    if (kk.rfind("float64", 0) == 0 ||
                        kk.rfind("int64", 0) == 0 ||
                        kk.rfind("uint64", 0) == 0) esz = 8;
                    else if (kk.rfind("float32", 0) == 0 ||
                             kk.rfind("int32", 0) == 0 ||
                             kk.rfind("uint32", 0) == 0) esz = 4;
                    else if (kk.rfind("int16", 0) == 0 ||
                             kk.rfind("uint16", 0) == 0) esz = 2;
                    else if (kk.rfind("char/utf8", 0) == 0 ||
                             kk.rfind("char/ascii", 0) == 0) esz = 1;
                    else if (kk.rfind("char/utf32", 0) == 0) esz = 4;
                    else if (kk.rfind("int8", 0) == 0 ||
                             kk.rfind("uint8", 0) == 0 ||
                             kk.rfind("bool", 0) == 0) esz = 1;
                    ndims[0] = esz > 0
                                   ? static_cast<std::int32_t>(mcap / esz)
                                   : static_cast<std::int32_t>(mcap);
                    ndim = 1;
                } else {
                    std::size_t i = 0;
                    while (i <= seg.size() && ndim < 8) {
                        const std::size_t comma = seg.find(',', i);
                        const std::string one =
                            comma == std::string::npos ? seg.substr(i)
                                                       : seg.substr(i, comma - i);
                        char* end = nullptr;
                        ndims[ndim++] = static_cast<std::int32_t>(
                            std::strtoll(one.c_str(), &end, 10));
                        if (comma == std::string::npos) break;
                        i = comma + 1;
                    }
                    kk = kk.substr(close + 1);
                }
            }
        }
        std::uint8_t* nb = nullptr;
        std::uint32_t nl = 0;
        ok = kvspaceXvalueEncode(
                 kk.c_str(), mb, static_cast<std::uint32_t>(mcap), ndims, ndim,
                 &nb, &nl) == 0 &&
             nb && c.vm.write_reg(c.f, c.ins.wr[0], nb, nl);
        std::free(nb);
    }
    return ok;
}

bool h_array_reshape(NativeCtx& c, std::string* err) {
    // xv·reshape(arr, dims...) -> a：body 字节原样，只换 dims（numel 守恒校验）。
    FReg& rr0 = c.f.regs[c.ins.rd[0]];
    std::uint8_t* rbody = nullptr;
    std::uint64_t rcap = 0;
    if (!c.vm.reg_body(c.f, c.ins.rd[0], &rbody, &rcap) || !rbody) {
        *err = "xv·reshape: bad array";
        return false;
    }
    std::uint8_t* rhd = rr0.view.head;
    const std::size_t rhcap =
        rhd ? static_cast<std::size_t>(rr0.view.headlen > 18
                                          ? rr0.view.headlen - 18
                                          : 0)
            : 0;
    const std::size_t rhlen =
        rhcap ? ::strnlen(reinterpret_cast<const char*>(rhd + 18), rhcap) : 0;
    const bin::LangType rt =
        c.vm.bin.langtype_of(reinterpret_cast<const char*>(rhd + 18), rhlen);
    const std::uint32_t resz = c.vm.bin.elem_width_of(rt);
    if (resz == 0) {
        *err = "xv·reshape: requires a compact array";
        return false;
    }
    const int ndims = c.ins.nrd - 1;
    if (ndims < 1 || ndims > 8) {
        *err = "xv·reshape: bad dim count";
        return false;
    }
    std::int32_t rdims[8] = {};
    std::int64_t numel = 1;
    for (int i = 0; i < ndims; i++) {
        std::int64_t dv = 0;
        if (!c.vm.read_i64(c.f, c.ins.rd[i + 1], &dv) || dv < 0) {
            *err = "xv·reshape: bad dim";
            return false;
        }
        rdims[i] = static_cast<std::int32_t>(dv);
        numel *= dv;
    }
    if (numel != static_cast<std::int64_t>(rcap / resz)) {
        *err = "xv·reshape: numel mismatch";
        return false;
    }
    const char* rkn = c.vm.kind_name(
        rt == bin::LangType::Float64      ? Scalar::K::Float
        : rt == bin::LangType::Float32    ? Scalar::K::Float
        : rt == bin::LangType::Int8 || rt == bin::LangType::Int16 ||
                  rt == bin::LangType::Int32 || rt == bin::LangType::Int64
              ? Scalar::K::Int
        : rt == bin::LangType::Uint8 || rt == bin::LangType::Uint16 ||
                  rt == bin::LangType::Uint32 || rt == bin::LangType::Uint64
              ? Scalar::K::Uint
              : Scalar::K::Bool,
        static_cast<std::uint8_t>(resz));
    if (!rkn) {
        *err = "xv·reshape: bad elem kind";
        return false;
    }
    std::uint8_t* nb = nullptr;
    std::uint32_t nl = 0;
    const bool ok = kvspaceXvalueEncode(
                        rkn, rbody, static_cast<std::uint32_t>(rcap), rdims,
                        ndims, &nb, &nl) == 0 &&
                    nb && c.vm.write_reg(c.f, c.ins.wr[0], nb, nl);
    std::free(nb);
    return ok;
}

bool h_array_at_set(NativeCtx& c, std::string* err) {
    // compact 数组元素读/写（[N,M]kind，body 连续打包）。
    const bool is_set = c.fn == "compactarray/set";
    FReg& ar0 = c.f.regs[c.ins.rd[0]];
    std::uint8_t* ab0 = nullptr;
    std::uint64_t acap0 = 0;
    // No operand prefetch for this op: resolve the lazy key read_reg would have.
    if (!ar0.src_ptr)
        c.vm.ensure_reg_key(c.f, ar0);
    if (!c.vm.reg_body(c.f, c.ins.rd[0], &ab0, &acap0) || !ab0) {
        *err = "xv: bad array";
        return false;
    }
    std::int32_t dims[8];
    std::uint32_t ndim;
    std::uint32_t esz;
    Scalar::K ekind;
    bool is_char;
    if (ar0.arr_ndim) {
        ndim = ar0.arr_ndim;
        esz = ar0.arr_esz;
        ekind = static_cast<Scalar::K>(ar0.arr_kind & 0x7F);
        is_char = (ar0.arr_kind & 0x80) != 0;
        std::memcpy(dims, ar0.arr_dims, sizeof(std::int32_t) * ndim);
    } else {
        const std::uint8_t* hd = ar0.view.head;
        const std::size_t hcap =
            hd ? static_cast<std::size_t>(ar0.view.headlen > 18
                                             ? ar0.view.headlen - 18
                                             : 0)
               : 0;
        const std::size_t hlen =
            hcap ? ::strnlen(reinterpret_cast<const char*>(hd + 18), hcap) : 0;
        const char* lt = reinterpret_cast<const char*>(hd + 18);
        std::size_t ln = hlen;
        ndim = 0;
        if (ln && *lt == '[') {
            const std::size_t close = std::string(lt, ln).find(']');
            if (close != std::string::npos) {
                const std::string seg(lt + 1, close - 1);
                std::size_t i = 0;
                while (i <= seg.size() && ndim < 8) {
                    const std::size_t comma = seg.find(',', i);
                    const std::string one =
                        comma == std::string::npos ? seg.substr(i)
                                                   : seg.substr(i, comma - i);
                    char* end = nullptr;
                    std::int64_t dv = std::strtoll(one.c_str(), &end, 10);
                    dims[ndim++] = static_cast<std::int32_t>(dv);
                    if (comma == std::string::npos) break;
                    i = comma + 1;
                }
                const std::size_t koff = close + 1;
                lt += koff;
                ln -= koff;
            }
        }
        const bin::LangType bt = c.vm.bin.langtype_of(lt, ln);
        is_char = bt == bin::LangType::CharUtf8 ||
                  bt == bin::LangType::CharUtf32 ||
                  bt == bin::LangType::CharAscii;
        esz = c.vm.bin.elem_width_of(bt);
        ekind = bt == bin::LangType::Float32 || bt == bin::LangType::Float64
                    ? Scalar::K::Float
                : bt == bin::LangType::Int8 || bt == bin::LangType::Int16 ||
                          bt == bin::LangType::Int32 ||
                          bt == bin::LangType::Int64
                    ? Scalar::K::Int
                : bt == bin::LangType::Uint8 || bt == bin::LangType::Uint16 ||
                          bt == bin::LangType::Uint32 ||
                          bt == bin::LangType::Uint64
                    ? Scalar::K::Uint
                    : Scalar::K::Bool;
        ar0.arr_esz = esz;
        ar0.arr_ndim = ndim;
        ar0.arr_kind =
            static_cast<std::uint8_t>(ekind) | (is_char ? 0x80 : 0);
        std::memcpy(ar0.arr_dims, dims, sizeof(std::int32_t) * ndim);
    }
    const int nidx = is_set ? c.ins.nrd - 2 : c.ins.nrd - 1;
    if (esz == 0 || ndim == 0 || nidx != (int)ndim) {
        *err = "xv: bad type/dims";
        return false;
    }
    std::int64_t flat = 0;
    for (int d = 0; d < nidx; d++) {
        std::int64_t idxv = 0;
        if (!c.vm.read_i64(c.f, c.ins.rd[d + 1], &idxv)) {
            *err = "xv: bad index";
            return false;
        }
        flat = flat * dims[d] + idxv;
    }
    const std::uint64_t off = (std::uint64_t)flat * esz;
    if ((std::uint64_t)flat * esz + esz > acap0) {
        *err = "xv: index out of range";
        return false;
    }
    if (!is_set) {
        Scalar ev{};
        ev.k = ekind;
        ev.width = static_cast<std::uint8_t>(esz);
        if (ev.k == Scalar::K::Float) {
            if (esz == 8) std::memcpy(&ev.f, ab0 + off, esz);
            else {
                float f2;
                std::memcpy(&f2, ab0 + off, esz);
                ev.f = f2;
            }
        } else if (ev.k == Scalar::K::Int) {
            std::int64_t v = 0;
            std::memcpy(&v, ab0 + off, esz);
            ev.i = v;
            ev.u = (std::uint64_t)v;
        } else {
            std::uint64_t v = 0;
            std::memcpy(&v, ab0 + off, esz);
            ev.u = v;
            ev.i = (std::int64_t)v;
        }
        if (is_char) {
            std::uint8_t u32[4] = {0};
            std::memcpy(u32, ab0 + off, esz < 4 ? esz : 4);
            std::uint8_t* eb = nullptr;
            std::uint32_t el = 0;
            const std::int32_t cdims[1] = {1};
            const bool ok = kvspaceXvalueEncode(
                                esz == 4 ? "char/utf32" : "char/utf8", u32, esz,
                                cdims, 1, &eb, &el) == 0 &&
                            eb && c.vm.write_reg(c.f, c.ins.wr[0], eb, el);
            std::free(eb);
            return ok;
        }
        if (ev.k == Scalar::K::Int && esz == 8) {
            return c.vm.slot_write_i64(c.f, c.ins.wr[0], ev.i);
        }
        // Same-type slot: patch the body in place; slot_write_scalar re-encodes otherwise.
        if (!c.vm.slot_write_scalar(c.f, c.ins.wr[0], ev,
                                    static_cast<std::uint8_t>(esz)))
            return false;
        c.vm.sync_fast_after_write(c.f, c.ins.wr[0], ev);
        return true;
    }
    // 写元素：值读参最后。同槽原地 patch / 异槽 TypeError。
    std::uint8_t tmp[8] = {0};
    FReg& vr0 = c.f.regs[c.ins.rd[c.ins.nrd - 1]];
    const FReg& vr = vr0.src_ptr ? *vr0.src_ptr : vr0;
    // read_reg trusts the cache only for aliases and frame locals; mirror that.
    const bool vtrust =
        vr.fast_i64 && esz == 8 && ekind == Scalar::K::Int && !is_char &&
        (vr0.src_ptr || (c.vm.ensure_reg_key(c.f, vr0) &&
                         c.vm.is_frame_local(c.f, vr0.key)));
    Wire vw{};
    kvspaceXh vh{};
    if (vtrust) {
        std::memcpy(tmp, &vr.fv, 8);  // same bytes the int64 wire body would carry
    } else if (!c.vm.read_reg(c.f, c.ins.rd[c.ins.nrd - 1], &vw)) {
        *err = "xv: bad value";
        return false;
    } else if (kvspaceXhDecode(vw.d, vw.n, &vh) == 0 && vh.body) {
        const std::uint32_t take = vh.content_len < esz ? vh.content_len : esz;
        std::memcpy(tmp, vh.body, take);
    } else if (vw.n <= 8) {
        std::memcpy(tmp, vw.d, vw.n);
    }
    FReg& ar = c.f.regs[c.ins.rd[0]];
    const RegDesc& rd0 = c.seg->regs[c.ins.rd[0]];
    const RegDesc& wd0 = c.seg->regs[c.ins.wr[0]];
    const bool same_slot = !rd0.name.empty() && rd0.name == wd0.name;
    if (same_slot && c.vm.reg_body(c.f, c.ins.rd[0], &ab0, &acap0) && ab0 &&
        (std::uint64_t)flat * esz + esz <= acap0) {
        ar.body = ab0;
        ar.cap = acap0;
        std::memcpy(ab0 + (std::uint64_t)flat * esz, tmp, esz);
        ar.fast_i64 = false;
        ar.fast_bool = false;
        return true;
    }
    *err = "TypeError: compactarray·set requires same write slot "
           "(`-> a`); value semantics use compactarray·copy first";
    return false;
}

// ── struct·new ─────────────────────────────────────────────────────────────
// 克隆 /lib/Name 原型子树到写槽，覆盖给定字段（校验字段存在性+类型）。
// in[0]=structref（"/lib/Name"），其后成对 (字段名, 值)。实例基值 langtype=structref。
namespace {
// 剥 `[dims]` 前缀得裸 kind（字段类型与值类型比对用）。
std::string bare_kind(const std::string& lt) {
    if (!lt.empty() && lt[0] == '[') {
        const std::size_t close = lt.find(']');
        if (close != std::string::npos)
            return lt.substr(close + 1);
    }
    return lt;
}
}  // namespace

bool h_struct_new(NativeCtx& c, std::string* err) {
    // ref 形参：绝对路径字面量（`/lib/Point`）的 RegDesc.name 即路径本体——
    // read_reg 会按 key 直达目标值（def struct 标记），不能当文本读；变量则读值。
    const RegDesc& rd0 = c.seg->regs[c.ins.rd[0]];
    std::string ref;
    if (rd0.kind == Operand::Kind::Literal && !rd0.name.empty() &&
        rd0.name[0] == '/') {
        ref = rd0.name.substr(0, rd0.name.find(' '));
    } else if (!c.atext(0, &ref) || ref.empty()) {
        *err = "TypeError: struct·new requires a type";
        return false;
    }
    if (ref.empty()) {
        *err = "TypeError: struct·new requires a type";
        return false;
    }
    // 原型必须存在且 kind=def struct。
    const std::uint8_t* pd = nullptr;
    std::uint32_t pn = 0;
    std::string plt;
    if (!read_wire(c.f.h, ref, &pd, &pn) || !v_langtype(Wire{pd, pn}, &plt) ||
        bare_kind(plt) != "def struct") {
        *err = "TypeError: " + ref + " is not a struct type";
        return false;
    }
    FReg& wr0 = c.f.regs[c.ins.wr[0]];
    if (!c.vm.ensure_reg_key(c.f, wr0)) {
        *err = "struct·new: bad write slot";
        return false;
    }
    const std::string base = wr0.key;
    char eb[192] = {};
    // 克隆原型成员（默认值：`Point·x` 等）+ 基值，覆盖给定字段。
    if (kvspaceCpList(c.f.h, ref.c_str(), base.c_str(), eb, sizeof eb) != 0) {
        *err = std::string("struct·new: copy failed: ") + eb;
        return false;
    }
    // 基值标 structref 标记（langtype=ref，空 body，ndim 0）。
    std::uint8_t* mark = nullptr;
    std::uint32_t ml = 0;
    bool ok = kvspaceXvalueEncode(ref.c_str(), nullptr, 0, nullptr, 0, &mark,
                                  &ml) == 0 &&
              mark;
    if (ok)
        ok = kvspaceSetValue(c.f.h, base.c_str(), mark, ml, 0, 0, eb,
                             sizeof eb) == 0;
    std::free(mark);
    if (!ok) {
        *err = std::string("struct·new: marker failed: ") + eb;
        return false;
    }
    // 字段成对：读参[1]=字段名,[2]=值,[3]=名,[4]=值,…
    for (int i = 1; i + 1 < c.ins.nrd; i += 2) {
        std::string fname;
        Wire fw{};
        Wire fv{};
        if (!c.vm.read_reg(c.f, c.ins.rd[i], &fw) ||
            !wire_text(fw.d, fw.n, &fname) || fname.empty() ||
            !c.vm.read_reg(c.f, c.ins.rd[i + 1], &fv)) {
            *err = "struct·new: bad field pair";
            return false;
        }
        // 字段类型槽：`ref/<fname>` 的 def langtype body。
        std::string ftype;
        {
            const std::uint8_t* td = nullptr;
            std::uint32_t tn = 0;
            const std::string tk = ref + "/" + fname;
            std::string tlt;
            if (!read_wire(c.f.h, tk, &td, &tn) ||
                !v_langtype(Wire{td, tn}, &tlt) ||
                bare_kind(tlt) != "def langtype") {
                *err = "TypeError: struct " + ref + " has no field " + fname;
                return false;
            }
            kvspaceXh th{};
            if (kvspaceXhDecode(td, tn, &th) != 0 || !th.body)
                ftype.clear();
            else
                ftype.assign(reinterpret_cast<const char*>(th.body),
                             th.content_len);
        }
        std::string vlt;
        (void)v_langtype(fv, &vlt);
        const bool none = v_is_none(fv);
        // *T 字段：None 合法；非 None 须 Ptr 且目标种类匹配（剥离前导 *）。
        if (!ftype.empty() && ftype[0] == '*') {
            if (!none &&
                (!v_ptr(fv) ||
                 bare_kind(ftype.substr(1)) != bare_kind(vlt))) {
                *err = "TypeError: field " + fname + ": expected " + ftype +
                       ", got " + (vlt.empty() ? "None" : vlt);
                return false;
            }
        } else if (!ftype.empty() &&
                   (none || bare_kind(ftype) != bare_kind(vlt))) {
            *err = "TypeError: field " + fname + ": expected " + ftype +
                   ", got " + (none ? "None" : vlt);
            return false;
        }
        const std::string mk = base + "\xC2\xB7" + fname;
        if (kvspaceSetValue(c.f.h, mk.c_str(), fv.d, fv.n, 0, 0, eb,
                            sizeof eb) != 0) {
            *err = std::string("struct·new: set field failed: ") + eb;
            return false;
        }
    }
    return true;
}

// ── 容器（obj / map·new）───────────────────────────────────────────────────
bool h_obj_map(NativeCtx& c, std::string* err) {
    std::string lt = c.write_slot_lt();
    if (lt.empty())
        lt = "map";
    std::uint8_t* mark = nullptr;
    std::uint64_t ml = 0;
    const std::string ck = c.f.regs[c.ins.wr[0]].key;
    bool ok = kvspaceXhNewShort(lt.c_str(), nullptr, 0, &mark, &ml) == 0 &&
              mark &&
              c.vm.write_reg(c.f, c.ins.wr[0], mark,
                             static_cast<std::uint32_t>(ml));
    std::free(mark);
    if (ok) {
        char e[192] = {};
        const std::string dir = ck + "/";
        ok = kvspaceMkindex(c.f.h, dir.c_str(), 0, e, sizeof e) == 0;
    }
    if (ok && (c.fn == "map/new" || c.ins.nrd >= 2)) {
        const std::string base_key = c.f.regs[c.ins.wr[0]].key;
        char e[192] = {};
        if (c.fn == "map/new") {
            for (int i = 0; i < c.ins.nrd; i++) {
                Wire ew{};
                if (!c.vm.read_reg(c.f, c.ins.rd[i], &ew)) {
                    *err = "map: bad element " + std::to_string(i);
                    return false;
                }
                const std::string mk =
                    base_key + "\xC2\xB7[" + std::to_string(i) + "]";
                if (kvspaceSetValue(c.f.h, mk.c_str(), ew.d, ew.n, 0, 0, e,
                                    sizeof e) != 0) {
                    *err = "map: set failed: " + c.ins.name;
                    return false;
                }
            }
        } else {
            // obj 命名成员：读参 [0]=键名,[1]=值,[2]=键名,[3]=值,…
            for (int i = 0; i + 1 < c.ins.nrd; i += 2) {
                Wire kw{};
                Wire vw{};
                std::string kname;
                if (!c.vm.read_reg(c.f, c.ins.rd[i], &kw) ||
                    !wire_text(kw.d, kw.n, &kname) || kname.empty() ||
                    !c.vm.read_reg(c.f, c.ins.rd[i + 1], &vw)) {
                    *err = "obj: bad member pair " + c.ins.name;
                    return false;
                }
                const std::string mk = base_key + "\xC2\xB7" + kname;
                if (kvspaceSetValue(c.f.h, mk.c_str(), vw.d, vw.n, 0, 0, e,
                                    sizeof e) != 0) {
                    *err = "obj: set failed: " + c.ins.name;
                    return false;
                }
            }
        }
    }
    return ok;
}

// ── kvlang（abs）───────────────────────────────────────────────────────────
bool h_kvlang_abs(NativeCtx& c, std::string* err) {
    // &x ≡ kvlang·abs(x)：读参是变量名/字面量路径 → 产 Ptr。
    std::string k0;
    const RegDesc& ad = c.seg->regs[c.ins.rd[0]];
    bool ok = true;
    if (ad.kind == Operand::Kind::Literal) {
        ok = c.atext(0, &k0);
    } else if (ad.name[0] == '/') {
        k0 = ad.name.substr(0, ad.name.find(' '));  // 剥类型后缀
    } else {
        // 变量帧 key 惰性：`&x` 前 x 未必写过（链表 `{next=&n3}` 先于 n3 再写），
        // 必须 ensure_reg_key 才拿得到路径（&n3 恒指向已建实例的槽 key）。
        FReg& ar = c.f.regs[c.ins.rd[0]];
        if (!c.vm.ensure_reg_key(c.f, ar))
            return false;
        k0 = ar.key;
    }
    if (!ok || k0.empty()) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    const std::uint8_t* td = nullptr;
    std::uint32_t tn = 0;
    std::string lt;
    if (read_wire(c.f.h, k0, &td, &tn))
        (void)v_langtype(Wire{td, tn}, &lt);
    std::uint8_t* ptr = nullptr;
    std::uint32_t pl = 0;
    ok = kvspaceNewPtr(lt.c_str(), k0.c_str(), &ptr, &pl) == 0 && ptr &&
         c.vm.write_reg(c.f, c.ins.wr[0], ptr, pl);
    std::free(ptr);
    return ok;
}

// ── kvspace ─────────────────────────────────────────────────────────────────
// kvspace 写失败：errbuf 优先，空则统一 "write failed"（对齐原末尾检查）。
bool kv_fail(NativeCtx& c, bool ok, char* errbuf, std::string* err) {
    if (ok)
        return true;
    *err = errbuf[0] ? std::string(c.ins.name) + ": " + errbuf
                     : "native: write failed: " + c.ins.name;
    return false;
}

bool h_kv_del(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    char errbuf[256] = {};
    const char* keys[1] = {k0.c_str()};
    const bool ok = kvspaceDel(c.f.h, keys, 1, errbuf, sizeof errbuf) == 0;
    return kv_fail(c, ok, errbuf, err);
}

bool h_kv_deltree(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    char errbuf[256] = {};
    const bool ok = kvspaceDelTree(c.f.h, k0.c_str(), errbuf, sizeof errbuf) == 0;
    return kv_fail(c, ok, errbuf, err);
}

bool h_kv_mkindex(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    char errbuf[256] = {};
    std::uint32_t cap = 0;
    bool ok = true;
    if (c.ins.nrd >= 2) {
        std::int64_t cv = 0;
        ok = c.ai64(1, &cv);
        cap = (std::uint32_t)cv;
    }
    ok = ok && kvspaceMkindex(c.f.h, k0.c_str(), cap, errbuf, sizeof errbuf) == 0;
    return kv_fail(c, ok, errbuf, err);
}

bool h_kv_list(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    std::string dkey = k0;
    if (!dkey.empty() && dkey.back() != '/' &&
        !(dkey.size() >= 2 &&
          (unsigned char)dkey[dkey.size() - 2] == 0xC2 &&
          (unsigned char)dkey.back() == 0xB7))
        dkey += "\xC2\xB7";
    char errbuf[256] = {};
    bool ok = true;
    if (c.fn == "kvspace/listlen") {
        if (g_trace)
            std::fprintf(stderr, "[listlen k0='%s' dkey='%s']\n",
                         k0.c_str(), dkey.c_str());
        std::int32_t cnt = 0;
        ok = kvspaceListLen(c.f.h, dkey.c_str(), 0, 0, &cnt) == 0 &&
             c.vm.write_named_i64(c.f.h, c.ins.wr[0], c.f, "int64", cnt);
    } else {
        std::int64_t idx = 0;
        std::uint8_t name[512];
        std::uint32_t nn = 0;
        ok = c.ai64(1, &idx) &&
             kvspaceListAt(c.f.h, dkey.c_str(), 0, 0, (std::int32_t)idx, name,
                           sizeof name, &nn) == 0;
        if (ok) {
            FReg& wr = c.f.regs[c.ins.wr[0]];
            ok = c.vm.ensure_reg_key(c.f, wr) &&
                 write_text(c.f.h, wr.key,
                            std::string_view(
                                reinterpret_cast<const char*>(name), nn));
        }
    }
    return kv_fail(c, ok, errbuf, err);
}

bool h_kv_cp(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    char errbuf[256] = {};
    const int rc = c.fn == "kvspace/cp"
                       ? kvspaceCp(c.f.h, k0.c_str(), k1.c_str(), errbuf,
                                   sizeof errbuf)
                       : c.fn == "kvspace/cpdir"
                             ? kvspaceCpTree(c.f.h, k0.c_str(), k1.c_str(),
                                             errbuf, sizeof errbuf)
                             : kvspaceCpList(c.f.h, k0.c_str(), k1.c_str(),
                                             errbuf, sizeof errbuf);
    return kv_fail(c, rc == 0, errbuf, err);
}

bool h_kv_extindex(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    char errbuf[256] = {};
    const bool ok =
        kvspaceMkindexExt(c.f.h, k0.c_str(), k1.c_str(), errbuf,
                          sizeof errbuf) == 0;
    return kv_fail(c, ok, errbuf, err);
}

bool h_kv_rmindexext(NativeCtx& c, std::string* err) {
    std::string k0, k1;
    if (!c.kv_paths(&k0, &k1)) {
        *err = "native: bad path operand: " + c.ins.name;
        return false;
    }
    char errbuf[256] = {};
    const bool ok = kvspaceRmindexExt(c.f.h, k0.c_str(), errbuf,
                                      sizeof errbuf) == 0;
    return kv_fail(c, ok, errbuf, err);
}

// ── 两级表：族前缀表 → 族内成员表（constexpr，无哈希）────────────────────
struct NativeCap {
    std::string_view name;  // 折叠名（`·`→`/`）= 表键
    NativeFn fn;
};

#define NCAP(name, fn) NativeCap{name, fn}

constexpr NativeCap kTimeCaps[] = {
    NCAP("time/now", h_time_now),
    NCAP("time/add", h_time_arith),
    NCAP("time/sub", h_time_arith),
    NCAP("time/before", h_time_arith),
    NCAP("time/after", h_time_arith),
    NCAP("time/duration/add", h_time_arith),
    NCAP("time/duration/sub", h_time_arith),
    NCAP("time/duration/before", h_time_arith),
    NCAP("time/duration/after", h_time_arith),
    NCAP("time/duration/nanos", h_duration_scale),
    NCAP("time/duration/millis", h_duration_scale),
    NCAP("time/duration/seconds", h_duration_scale),
    NCAP("time/duration/minutes", h_duration_scale),
    NCAP("time/duration/hours", h_duration_scale),
    NCAP("time/duration/as_nanos", h_duration_as),
    NCAP("time/duration/as_millis", h_duration_as),
    NCAP("time/duration/as_seconds", h_duration_as),
    NCAP("time/duration/as_minutes", h_duration_as),
    NCAP("time/duration/as_hours", h_duration_as),
};

constexpr NativeCap kRandomCaps[] = {
    NCAP("random/int63", h_random),
    NCAP("random/uint64", h_random),
    NCAP("random/intn", h_random),
};

constexpr NativeCap kArrayCaps[] = {
    NCAP("compactarray/new", h_array_new),
    NCAP("compactarray/fill", h_array_fill),
    NCAP("compactarray/copy", h_array_copy),
    NCAP("compactarray/newslice", h_array_newslice),
    NCAP("compactarray/numel", h_array_meta),
    NCAP("compactarray/dim", h_array_meta),
    NCAP("compactarray/shape", h_array_meta),
    NCAP("compactarray/langtype", h_xv_meta),
    NCAP("compactarray/bodylen", h_xv_meta),
    NCAP("compactarray/parselangtype", h_xv_meta),
    NCAP("compactarray/newreinterpret", h_xv_meta),
    NCAP("compactarray/newreshape", h_array_reshape),
    NCAP("compactarray/at", h_array_at_set),
    NCAP("compactarray/set", h_array_at_set),
};

constexpr NativeCap kScatterCaps[] = {
    NCAP("scatterarray/new", h_scatter_new),
    NCAP("scatterarray/len", h_scatter_len),
    NCAP("scatterarray/at", h_scatter_at),
    NCAP("scatterarray/set", h_scatter_set),
    NCAP("scatterarray/newcompact", h_scatter_newcompact),
    NCAP("scatterarray/newscatter", h_scatter_newscatter),
    NCAP("scatterarray/append", h_scatter_append),
    NCAP("scatterarray/slice", h_scatter_slice),
};

constexpr NativeCap kStringCaps[] = {
    NCAP("string/char", h_string_char),
    NCAP("string/cmp", h_string_cmp),
    NCAP("string/concat", h_string_concat),
    NCAP("string/find", h_string_find),
    NCAP("string/formatint", h_string_formatint),
    NCAP("string/formatuint", h_string_formatuint),
    NCAP("string/len", h_string_len),
    NCAP("string/ord", h_string_ord),
    NCAP("string/parsefloat", h_string_parsefloat),
    NCAP("string/parseint", h_string_parseint),
    NCAP("string/parseuint", h_string_parseuint),
    NCAP("string/set", h_string_set),
    NCAP("string/slice", h_string_slice),
};

constexpr NativeCap kContainerCaps[] = {
    NCAP("obj", h_obj_map),
    NCAP("map/new", h_obj_map),
    NCAP("struct/new", h_struct_new),
};

constexpr NativeCap kKvlangCaps[] = {
    NCAP("kvlang/abs", h_kvlang_abs),
};

constexpr NativeCap kKvspaceCaps[] = {
    NCAP("kvspace/del", h_kv_del),
    NCAP("kvspace/deltree", h_kv_deltree),
    NCAP("kvspace/mkindex", h_kv_mkindex),
    NCAP("kvspace/listlen", h_kv_list),
    NCAP("kvspace/listat", h_kv_list),
    NCAP("kvspace/cp", h_kv_cp),
    NCAP("kvspace/cpdir", h_kv_cp),
    NCAP("kvspace/cplist", h_kv_cp),
    NCAP("kvspace/extindex", h_kv_extindex),
    NCAP("kvspace/rmindexext", h_kv_rmindexext),
};

struct NativeFamily {
    std::string_view prefix;      // 族前缀（含尾 `/`，obj 等单名精确匹配）
    const NativeCap* caps;
    std::size_t n;
};

#define NCAPS(a) sizeof(a) / sizeof((a)[0])

constexpr NativeFamily kFamilies[] = {
    {"time/", kTimeCaps, NCAPS(kTimeCaps)},
    {"random/", kRandomCaps, NCAPS(kRandomCaps)},
    {"compactarray/", kArrayCaps, NCAPS(kArrayCaps)},
    {"scatterarray/", kScatterCaps, NCAPS(kScatterCaps)},
    {"string/", kStringCaps, NCAPS(kStringCaps)},
    {"obj", kContainerCaps, NCAPS(kContainerCaps)},
    {"map/", kContainerCaps, NCAPS(kContainerCaps)},
    {"struct/", kContainerCaps, NCAPS(kContainerCaps)},
    {"kvlang/", kKvlangCaps, NCAPS(kKvlangCaps)},
    {"kvspace/", kKvspaceCaps, NCAPS(kKvspaceCaps)},
};

#undef NCAP
#undef NCAPS

}  // namespace

// 折叠名 → handler；未命中 = 表未命中（未实现 op）。
const NativeFn* find_native(std::string_view fn) {
    for (const NativeFamily& fam : kFamilies) {
        if (!fn.starts_with(fam.prefix))
            continue;
        for (std::size_t i = 0; i < fam.n; i++) {
            if (fam.caps[i].name == fn)
                return &fam.caps[i].fn;
        }
    }
    return nullptr;
}

}  // namespace kv::rt

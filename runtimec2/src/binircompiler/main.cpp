// binircompiler —— KV 函数 → binir 序列的可执行入口。
//
// 与运行时用同一份编译代码（src/binircompiler/compiler.cpp）；本文件只做参数解析、
// 现场 layout（--file，含 stdlib/--lib 引导）与结果输出。
//
// 用法：
//   binircompiler --kvspace <dsn> --func <path> [arg...] [选项]
//   binircompiler --file <x.kv>   --func <name> [arg...] [选项]
//
// 实参字面量 → 类型：整数 int64、小数 float64、"…" char/utf8、true/false bool。
// 不传实参且没有 --generic：按零参特化编译；函数有读参则报「缺少实参」。
// --generic：不提供类型信息，走通用编译（与运行时行为逐字节相同）。
//
// --file 的引导：先按运行时同一套 .kv 集合 layout 库（stdlib/ 全部，再加 --lib），
// 然后才 layout 用户文件；--no-boot 跳过库引导。
//
// 退出码：0 成功；1 编译失败；2 用法/环境错误；3 libkvlanglayout.so 缺符号。

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <kvspace/kvspace.h>

#include "binir_dump.hpp"
#include "binircompiler/compiler.hpp"

namespace {

using kvlang::runtimec2::binircompiler::ArgumentType;
using kvlang::runtimec2::binircompiler::CompileError;
using kvlang::runtimec2::binircompiler::CompileOptions;
using kvlang::runtimec2::binircompiler::Segment;
using kvlang::runtimec2::binircompiler::SpecializationReport;

constexpr int kExitOk = 0;
constexpr int kExitCompile = 1;
constexpr int kExitUsage = 2;
constexpr int kExitLayoutAbi = 3;

const char* kUsage =
    "usage:\n"
    "  binircompiler --kvspace <dsn> --func <path> [arg...] [options]\n"
    "  binircompiler --file <x.kv>   --func <name> [arg...] [options]\n"
    "\n"
    "options:\n"
    "  --dump           print the binir sequence (same format as RC2_DUMP_BINIR=1)\n"
    "  --out <path>     write raw binir bytes\n"
    "  --check          encode -> decode -> same_program round-trip\n"
    "  --compare        compile generic + specialized, list per-instruction diff\n"
    "  --generic        no argument types: generic compilation (runtime path)\n"
    "  --no-boot        --file: skip stdlib/--lib boot (bare user file only)\n"
    "  --lib <path>     --file: extra .kv file or directory to layout after stdlib\n"
    "  -h, --help       show this help\n"
    "\n"
    "argument literals: integer -> int64, decimal -> float64,\n"
    "                   \"...\" -> char/utf8, true|false -> bool\n"
    "\n"
    "exit: 0 ok, 1 compile failure, 2 usage/environment error,\n"
    "      3 layout library missing kvlangLayoutFile (version mismatch)\n";

int usage_error(const std::string& message) {
    std::cerr << "binircompiler: " << message << "\n\n" << kUsage;
    return kExitUsage;
}

// 字面量 → 实参类型（唯一映射，不做数值范围猜测）。
bool literal_argument(const std::string& text, ArgumentType* out,
                      std::string* why) {
    if (text == "true" || text == "false") {
        out->langtype = "bool";
        return true;
    }
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        out->langtype = "char/utf8";
        return true;
    }
    std::size_t i = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        i++;
    bool digits = false, dot = false, exponent = false;
    for (; i < text.size(); i++) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            digits = true;
        } else if (c == '.' && !dot && !exponent) {
            dot = true;
        } else if ((c == 'e' || c == 'E') && digits && !exponent && i + 1 < text.size()) {
            exponent = true;
            if (text[i + 1] == '+' || text[i + 1] == '-')
                i++;
        } else {
            digits = false;
            break;
        }
    }
    if (!digits) {
        *why = "cannot derive an argument type from \"" + text +
               "\" (integer/decimal/\"string\"/true|false)";
        return false;
    }
    out->langtype = dot || exponent ? "float64" : "int64";
    return true;
}

std::string normalize_func(const std::string& raw) {
    std::string name = raw;
    while (name.size() > 5 && name.back() == '/')
        name.pop_back();
    if (!name.empty() && name[0] == '/')
        return name;
    return "/lib/" + name;
}

struct Args {
    std::string kvspace_dsn;
    std::string file;
    std::string func;
    std::string out_path;
    std::vector<ArgumentType> arguments;
    std::vector<std::string> extra_libs;
    bool no_boot = false;
    bool dump = false;
    bool check = false;
    bool compare = false;
    bool generic = false;
};

// 返回 -1 = 继续解析；>=0 = 直接退出的退出码。
int parse_args(int argc, char** argv, Args* args) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto value = [&](std::string* out) {
            if (i + 1 >= argc)
                return false;
            *out = argv[++i];
            return true;
        };
        if (a == "-h" || a == "--help") {
            std::cout << kUsage;
            return kExitOk;
        }
        if (a == "--kvspace") {
            if (!value(&args->kvspace_dsn))
                return usage_error("--kvspace needs a DSN");
        } else if (a == "--file") {
            if (!value(&args->file))
                return usage_error("--file needs a path");
        } else if (a == "--func") {
            if (!value(&args->func))
                return usage_error("--func needs a function path");
        } else if (a == "--out") {
            if (!value(&args->out_path))
                return usage_error("--out needs a path");
        } else if (a == "--dump") {
            args->dump = true;
        } else if (a == "--check") {
            args->check = true;
        } else if (a == "--compare") {
            args->compare = true;
        } else if (a == "--generic") {
            args->generic = true;
        } else if (a == "--no-boot") {
            args->no_boot = true;
        } else if (a == "--lib") {
            std::string lib;
            if (!value(&lib))
                return usage_error("--lib needs a path");
            args->extra_libs.push_back(std::move(lib));
        } else if (a.rfind("--", 0) == 0) {
            return usage_error("unknown option: " + a);
        } else {
            ArgumentType t;
            std::string why;
            if (!literal_argument(a, &t, &why))
                return usage_error(why);
            args->arguments.push_back(std::move(t));
        }
    }
    if (args->kvspace_dsn.empty() == args->file.empty())
        return usage_error("exactly one of --kvspace / --file is required");
    if (args->func.empty())
        return usage_error("--func is required");
    if (args->generic && !args->arguments.empty())
        return usage_error("--generic does not take argument literals");
    if (args->compare && args->generic)
        return usage_error("--compare needs argument types (drop --generic)");
    if (args->compare && (args->check || !args->out_path.empty()))
        return usage_error("--compare cannot be combined with --check / --out");
    if (args->file.empty() && (args->no_boot || !args->extra_libs.empty()))
        return usage_error("--no-boot / --lib only apply to --file");
    return -1;
}

using LayoutFileFn = int (*)(const char*, const char*, char*, unsigned int,
                             char*, unsigned int);

// --file：dlopen libkvlanglayout.so，进程内 layout（不 fork）。
// dlopen 失败 = 环境错误(2)；符号缺失 = ABI 不匹配(3)；layout 失败 = 源码错误(1)。
class LayoutApi {
  public:
    int open() {
        void* lib = dlopen("libkvlanglayout.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            const char* err = dlerror();
            std::cerr << "binircompiler: cannot load libkvlanglayout.so: "
                      << (err ? err : "unknown error")
                      << "\n  build it with `make layout` (repo lib/ must be "
                         "reachable from $ORIGIN/../lib)\n";
            return kExitUsage;
        }
        file_ = reinterpret_cast<LayoutFileFn>(dlsym(lib, "kvlangLayoutFile"));
        if (!file_) {
            std::cerr << "binircompiler: libkvlanglayout.so has no "
                         "kvlangLayoutFile symbol (library version mismatch)\n";
            dlclose(lib);
            return kExitLayoutAbi;
        }
        lib_ = lib;
        return kExitOk;
    }
    ~LayoutApi() {
        if (lib_)
            dlclose(lib_);
    }
    LayoutApi() = default;
    LayoutApi(const LayoutApi&) = delete;
    LayoutApi& operator=(const LayoutApi&) = delete;

    // 0 成功；非 0 = 源码错误，*error 带 layout 的诊断。
    int layout(const std::string& path, const std::string& dsn,
               std::string* error) const {
        char entry[256] = {};
        char buf[1024] = {};
        if (file_(path.c_str(), dsn.c_str(), entry, sizeof entry, buf,
                  sizeof buf) != 0) {
            *error = buf[0] ? buf : "unknown layout error";
            return kExitCompile;
        }
        return kExitOk;
    }

  private:
    void* lib_ = nullptr;
    LayoutFileFn file_ = nullptr;
};

std::filesystem::path executable_dir(const char* argv0) {
    std::error_code ec;
#if defined(__linux__)
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return self.parent_path();
#endif
    if (!argv0 || !*argv0)
        return {};
    const auto abs = std::filesystem::absolute(argv0, ec);
    return ec ? std::filesystem::path{} : abs.parent_path();
}

// stdlib 目录：$KVLANG_STDLIB 优先，否则 <exe>/../stdlib（repo 布局）。
std::string stdlib_dir(const char* argv0) {
    if (const char* env = std::getenv("KVLANG_STDLIB"); env && *env)
        return env;
    const std::filesystem::path dir = executable_dir(argv0);
    if (dir.empty())
        return {};
    return (dir / ".." / "stdlib").lexically_normal().string();
}

// 一个路径 → .kv 文件列表（目录递归、按路径排序，与 build.rs 内嵌顺序一致）。
bool collect_kv(const std::string& path, std::vector<std::string>* out,
                std::string* why) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        *why = "no such file or directory: " + path;
        return false;
    }
    if (std::filesystem::is_regular_file(path, ec)) {
        out->push_back(path);
        return true;
    }
    std::vector<std::string> found;
    for (auto it = std::filesystem::recursive_directory_iterator(path, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code fe;
        if (it->is_regular_file(fe) && it->path().extension() == ".kv")
            found.push_back(it->path().string());
    }
    if (ec) {
        *why = "cannot scan " + path + ": " + ec.message();
        return false;
    }
    std::sort(found.begin(), found.end());
    out->insert(out->end(), found.begin(), found.end());
    return true;
}

// 运行时（byteseek）的启动引导二进制：$KVLANG_BIN 优先，否则 <exe>/byteseek。
std::string boot_binary(const char* argv0) {
    if (const char* env = std::getenv("KVLANG_BIN"); env && *env)
        return env;
    const std::filesystem::path dir = executable_dir(argv0);
    if (dir.empty())
        return {};
    return (dir / "byteseek").lexically_normal().string();
}

// 注册运行时的 def rwir（println/print/json·* 等）：它们由 byteseek 的扩展能力
// 表持有，不是 stdlib .kv 里的代码。`byteseek isa publish` 就是为「layout CLI
// 无需完整 boot 即可独立工作」提供的注册入口；这里复用它，避免在 C++ 侧复制
// 第二份能力表。
int publish_runtime_registrations(const std::string& boot_bin,
                                  const std::string& dsn) {
    std::error_code ec;
    if (boot_bin.empty() || !std::filesystem::exists(boot_bin, ec)) {
        std::cerr << "binircompiler: help runtime not found ("
                  << (boot_bin.empty() ? "no executable path" : boot_bin)
                  << ")\n  build it with `make byteseek`, set KVLANG_BIN=<path>, "
                     "or pass --no-boot\n";
        return kExitUsage;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "binircompiler: fork failed\n";
        return kExitUsage;
    }
    if (pid == 0) {
        setenv("KVSPACE", dsn.c_str(), 1);
        execl(boot_bin.c_str(), boot_bin.c_str(), "isa", "publish",
              static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "binircompiler: `" << boot_bin
                  << " isa publish` failed (runtime rwir registration)\n";
        return kExitUsage;
    }
    return kExitOk;
}

// 按运行时的引导顺序 layout 一组库：多文件库允许前向引用，未解析的库多轮重试，
// 无进展即硬失败（与 byteseek engine::layout_libs 同构）。
// 只做 layout，不执行 init：编译只需要 /lib 的布局；init 的取值副作用（如
// /lib/math·Pi 的常量值）不在这里物化。
int boot_libraries(const LayoutApi& api, const std::vector<std::string>& files,
                   const std::string& dsn) {
    std::vector<std::size_t> pending;
    pending.reserve(files.size());
    for (std::size_t i = 0; i < files.size(); i++)
        pending.push_back(i);
    std::vector<std::string> last_errors(files.size());
    while (!pending.empty()) {
        std::vector<std::size_t> next;
        for (const std::size_t i : pending) {
            std::string error;
            if (api.layout(files[i], dsn, &error) != kExitOk) {
                last_errors[i] = std::move(error);
                next.push_back(i);
            }
        }
        if (next.size() == pending.size())
            break;  // 本轮无进展：剩下的都是真错误
        pending = std::move(next);
    }
    if (pending.empty())
        return kExitOk;
    for (const std::size_t i : pending) {
        std::cerr << "binircompiler: boot layout failed for " << files[i] << ": "
                  << last_errors[i] << "\n";
    }
    return kExitCompile;
}

std::string type_name(kvlang::runtimec2::vmbin::LangType lt) {
    const char* n = kvlang::runtimec2::vmbin::langtype_name(lt);
    return n ? n : "?";
}

// 编译期校验（阶段 A 的 union 校验入口 + #594 占位）。运行时路径不经过这里；
// 工具每次编译都跑，保证入口被真实执行。
bool run_compile_checks(const Segment& segment, const std::string& func_dir) {
    CompileError error;
    error.func_dir = func_dir;
    if (!kvlang::runtimec2::binircompiler::RunCompileChecks(segment, &error)) {
        std::cerr << "binircompiler: compile checks failed for " << func_dir
                  << ": " << error.reason << "\n";
        return false;
    }
    return true;
}

void print_report(const SpecializationReport& report, bool to_stderr) {
    std::FILE* out = to_stderr ? stderr : stdout;
    for (const auto& row : report.instructions) {
        if (row.from != row.to)
            std::fprintf(out, "[spec] pc=%zu %s -> %s ; %s\n", row.pc,
                         row.from.c_str(), row.to.c_str(), row.reason.c_str());
        else
            std::fprintf(out, "[spec] pc=%zu %s （未改写：%s）\n", row.pc,
                         row.from.c_str(), row.reason.c_str());
    }
    std::fflush(out);
}

int print_compare(const std::string& func_dir,
                  const std::vector<ArgumentType>& arguments,
                  const Segment& generic, const Segment& specialized,
                  const SpecializationReport& report) {
    std::cout << "func=" << func_dir << " args=";
    if (arguments.empty()) {
        std::cout << "(none)";
    } else {
        for (std::size_t i = 0; i < arguments.size(); i++) {
            if (i)
                std::cout << ",";
            std::cout << arguments[i].langtype;
        }
    }
    std::cout << "\n  pc  op                 -> resolved           note\n";
    for (const auto& row : report.instructions) {
        const char* mark = row.from != row.to ? "*" : " ";
        std::printf("  %3zu %s %-18s -> %-18s %s\n", row.pc, mark,
                    row.from.c_str(), row.to.c_str(), row.reason.c_str());
    }
    std::size_t generic_typed = 0, generic_dynamic = 0;
    for (const auto& ins : generic.insns) {
        if (ins.impl == kvlang::runtimec2::rwir::Impl::Typed)
            generic_typed++;
        else if (ins.impl == kvlang::runtimec2::rwir::Impl::Dynamic)
            generic_dynamic++;
    }
    std::printf("summary: typed %zu -> %zu, generic %zu -> %zu (insns=%zu)\n",
                generic_typed, report.typed, generic_dynamic, report.generic,
                report.instructions.size());
    if (generic.regs.size() == specialized.regs.size()) {
        for (std::size_t i = 0; i < generic.regs.size(); i++) {
            const auto& a = generic.regs[i];
            const auto& b = specialized.regs[i];
            if (a.langtype == b.langtype && a.width == b.width)
                continue;
            std::printf("reg[%zu] %-12s %s -> %s (%ub -> %ub)\n", i,
                        b.name.empty() ? b.slot_key.c_str() : b.name.c_str(),
                        type_name(a.langtype).c_str(),
                        type_name(b.langtype).c_str(), (unsigned)a.width,
                        (unsigned)b.width);
        }
    }
    return std::cout.good() ? kExitOk : kExitUsage;
}

// --file 的临时 shm：布局前建目录，退出前整棵删掉（含 .garb/.gen/.sbo.*）。
bool make_temp_dir(std::string* out) {
    const char* tmp = std::getenv("TMPDIR");
    std::string tmpl = std::string(tmp && *tmp ? tmp : "/tmp") +
                       "/binircompiler-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data()))
        return false;
    *out = buf.data();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (const int rc = parse_args(argc, argv, &args); rc >= 0)
        return rc;
    const std::string func_dir = normalize_func(args.func);

    std::string temp_dir;
    std::string dsn = args.kvspace_dsn;
    std::optional<LayoutApi> layout_api;
    auto cleanup = [&] {
        if (!temp_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(temp_dir, ec);
        }
    };
    if (!args.file.empty()) {
        if (!make_temp_dir(&temp_dir)) {
            std::cerr << "binircompiler: cannot create temporary directory\n";
            return kExitUsage;
        }
        dsn = "shm://" + temp_dir + "/store.shm";
        layout_api.emplace();
        if (const int rc = layout_api->open(); rc != kExitOk) {
            cleanup();
            return rc;
        }
        std::vector<std::string> libs;
        std::string why;
        if (!args.no_boot) {
            if (const int rc = publish_runtime_registrations(
                    boot_binary(argv[0]), dsn);
                rc != kExitOk) {
                cleanup();
                return rc;
            }
            const std::string dir = stdlib_dir(argv[0]);
            if (dir.empty() || !collect_kv(dir, &libs, &why)) {
                std::cerr << "binircompiler: stdlib not found ("
                          << (dir.empty() ? "no executable path" : why)
                          << ")\n  set KVLANG_STDLIB=<dir> or pass --no-boot\n";
                cleanup();
                return kExitUsage;
            }
        }
        for (const std::string& extra : args.extra_libs) {
            if (!collect_kv(extra, &libs, &why)) {
                std::cerr << "binircompiler: --lib " << extra << ": " << why
                          << "\n";
                cleanup();
                return kExitUsage;
            }
        }
        if (const int rc = boot_libraries(*layout_api, libs, dsn);
            rc != kExitOk) {
            cleanup();
            return rc;
        }
        std::string error;
        if (const int rc = layout_api->layout(args.file, dsn, &error);
            rc != kExitOk) {
            std::cerr << "binircompiler: layout failed for " << args.file << ": "
                      << error << "\n";
            cleanup();
            return rc;
        }
    }

    void* kvspace = kvspaceConnect(dsn.c_str());
    if (!kvspace) {
        std::cerr << "binircompiler: cannot connect to DSN: " << dsn << "\n";
        cleanup();
        return kExitUsage;
    }

    CompileOptions options;
    if (!args.generic)
        options.arguments = args.arguments;

    // --compare：通用版 + 特化版各编一次，逐条列差异。
    if (args.compare) {
        Segment generic;
        CompileError err;
        if (!kvlang::runtimec2::binircompiler::compile(
                kvspace, func_dir, CompileOptions{}, &generic, &err)) {
            std::cerr << "binircompiler: compile failed for " << func_dir << ": "
                      << err.reason << "\n";
            kvspaceClose(kvspace);
            cleanup();
            return kExitCompile;
        }
        Segment specialized;
        SpecializationReport report;
        if (!kvlang::runtimec2::binircompiler::compile(
                kvspace, func_dir, options, &specialized, &err, &report)) {
            std::cerr << "binircompiler: compile failed for " << func_dir << ": "
                      << err.reason << "\n";
            kvspaceClose(kvspace);
            cleanup();
            return kExitCompile;
        }
        if (!run_compile_checks(generic, func_dir) ||
            !run_compile_checks(specialized, func_dir)) {
            kvspaceClose(kvspace);
            cleanup();
            return kExitCompile;
        }
        if (args.dump) {
            print_report(report, /*to_stderr=*/true);
            kvlang::runtimec2::dump_binir_to(stdout, specialized, func_dir.c_str());
        }
        const int rc = print_compare(func_dir, args.arguments, generic,
                                     specialized, report);
        kvspaceClose(kvspace);
        cleanup();
        return rc;
    }

    Segment segment;
    CompileError error;
    SpecializationReport report;
    const bool want_report = options.arguments.has_value();
    if (!kvlang::runtimec2::binircompiler::compile(
            kvspace, func_dir, options, &segment, &error,
            want_report ? &report : nullptr)) {
        std::cerr << "binircompiler: compile failed for " << func_dir << ": "
                  << error.reason << "\n";
        kvspaceClose(kvspace);
        cleanup();
        return kExitCompile;
    }
    if (segment.insns.empty()) {
        std::cerr << "binircompiler: compile failed for " << func_dir
                  << ": no instruction slots (function not found)\n";
        kvspaceClose(kvspace);
        cleanup();
        return kExitCompile;
    }
    if (!run_compile_checks(segment, func_dir)) {
        kvspaceClose(kvspace);
        cleanup();
        return kExitCompile;
    }
    if (args.dump) {
        if (want_report)
            print_report(report, /*to_stderr=*/true);
        kvlang::runtimec2::dump_binir_to(stdout, segment, func_dir.c_str());
    }
    if (!args.out_path.empty()) {
        if (segment.binir.empty()) {
            std::cerr << "binircompiler: binir bytes unavailable for " << func_dir
                      << " (encode failed)\n";
            kvspaceClose(kvspace);
            cleanup();
            return kExitCompile;
        }
        std::ofstream out(args.out_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(segment.binir.data()),
                  static_cast<std::streamsize>(segment.binir.size()));
        out.close();
        if (!out) {
            std::cerr << "binircompiler: cannot write " << args.out_path << "\n";
            kvspaceClose(kvspace);
            cleanup();
            return kExitUsage;
        }
    }
    if (args.check) {
        std::string why;
        if (!kvlang::runtimec2::binircompiler::check_roundtrip(segment, &why)) {
            std::cerr << "binircompiler: round-trip check failed for " << func_dir
                      << ": " << why << "\n";
            kvspaceClose(kvspace);
            cleanup();
            return kExitCompile;
        }
    }
    if (!args.dump && args.out_path.empty() && !args.check) {
        std::cout << "func=" << func_dir << " insns=" << segment.insns.size()
                  << " binir_bytes=" << segment.binir.size();
        if (want_report)
            std::cout << " typed=" << report.typed
                      << " generic=" << report.generic;
        std::cout << "\n";
    }
    kvspaceClose(kvspace);
    cleanup();
    return kExitOk;
}

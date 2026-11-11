//! 波 0：参数声明语法与禁止隐式解引用（spec 04-layout语义/03-函数、05-runtime语义/04-函数调用）。
//!
//! 两条规约：
//!
//! 1. **参数声明只有两种形态**：`*T #name`（传址）与 `T #name`（标量值复制）。
//!    类型在前、`#name` 在后；`#name` 是**注释名**（`#` 起行内注释），体内引用写**裸名**。
//!    `inplace` 是写参修饰符，写在**类型之前**：`inplace *T #name`。
//!    旧写法 `name: type` 不再接受。
//! 2. **禁止隐式解引用**：`·`、`·[i]`、`[i]` 作用在 `*T` 基上一律报错，必须写
//!    `(*p)·x` / `(*p)·[i]` / `(*p)[i]`。显式 `*` 是唯一解引用路径。
//!
//! 本文件是**判据**：先写测试再改 parser。每条正例给出波 0 后必须编过的输入，
//! 每条反例给出必须打红、且错误文本命中规约原文的输入。

use std::path::PathBuf;
use std::process::Command;

/// 测试用的临时目录，**离开作用域即删**（含 panic 展开时）。
/// 普通清理语句在断言失败时会被跳过——测试红了反而留下最多垃圾。
struct TempDir(PathBuf);

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

impl TempDir {
    fn join<P: AsRef<std::path::Path>>(&self, p: P) -> PathBuf {
        self.0.join(p)
    }
}

fn bin() -> &'static str {
    env!("CARGO_BIN_EXE_kvlanglayout")
}

fn temp_dir(tag: &str) -> TempDir {
    let dir =
        std::env::temp_dir().join(format!("kvlanglayout_wave0_{}_{}", tag, std::process::id()));
    // 先删同名旧目录：测试在同一 pod 反复跑会累积，/tmp 曾因此 OOM。
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

/// 跑一次 `kvlanglayout vet`，返回 (是否通过, 诊断文本)。
fn check(tag: &str, src: &str) -> (bool, String) {
    let dir = temp_dir(tag);
    let file = dir.join("p.kv");
    std::fs::write(&file, src).unwrap();
    let out = Command::new(bin())
        .args(["vet", file.to_str().unwrap()])
        .output()
        .unwrap();
    let text = format!(
        "{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
    (out.status.success(), text)
}

fn assert_ok(tag: &str, src: &str) {
    let (ok, text) = check(tag, src);
    assert!(ok, "{tag} 应通过，实际诊断：\n{text}");
}

fn assert_err(tag: &str, src: &str, expected: &str) {
    let (ok, text) = check(tag, src);
    assert!(!ok, "{tag} 应被拒绝，却通过了");
    assert!(
        text.contains(expected),
        "{tag} 的错误文本要含 {expected:?}，实际：\n{text}"
    );
}

// ── 正例：波 0 后必须编过 ──────────────────────────────────────────

/// 新参数语法 `*T #name` / `T #name` 必须被接受，体内引用写裸名。
#[test]
fn new_parameter_syntax_is_accepted() {
    assert_ok(
        "newsyntax",
        "rwfunc add_one(int64 #n) -> (int64 #r) {\n\tn + 1 -> r\n}\n",
    );
}

/// 指针参数在体内用 `*name` 解引用后做成员访问：`(*p)·x`。
#[test]
fn explicit_deref_member_access_is_allowed() {
    assert_ok(
        "deref-member",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\n\
         rwfunc read(*Point #p) -> (int64 #r) {\n\t(*p)·x -> r\n}\n",
    );
}

/// 指针参数显式解引用后做坐标访问：`(*p)·[i]`。
#[test]
fn explicit_deref_coordinate_access_is_allowed() {
    assert_ok(
        "deref-coord",
        "rwfunc first(*int64·int64 #p) -> (int64 #r) {\n\t(*p)·[1] -> r\n}\n",
    );
}

// ── 反例：波 0 后必须打红，且文本命中规约原文 ────────────────────

/// 错误 1：`p·x`（p 为 *Point）必须报「对指针类型的成员访问必须显式解引用」。
#[test]
fn implicit_deref_on_member_access_is_rejected() {
    assert_err(
        "implicit-member",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\n\
         rwfunc read(*Point #p) -> (int64 #r) {\n\tp·x -> r\n}\n",
        "对指针类型的成员访问必须显式解引用",
    );
}

/// 错误 2：`p·[i]`（p 为 *map）必须报「对指针类型的坐标访问必须显式解引用」。
#[test]
fn implicit_deref_on_coordinate_access_is_rejected() {
    assert_err(
        "implicit-coord",
        "rwfunc first(*int64·int64 #p) -> (int64 #r) {\n\tp·[1] -> r\n}\n",
        "对指针类型的坐标访问必须显式解引用",
    );
}

/// 错误 4：旧写法 `name: type` 不再接受。
#[test]
fn old_parameter_syntax_is_rejected() {
    assert_err(
        "oldsyntax",
        "rwfunc add_one(n:int64) -> (r:int64) {\n\tn + 1 -> r\n}\n",
        "参数声明必须是 `*T #name` 或 `T #name`",
    );
}

/// 错误 5：值复制参数只能是字符串、数值或 bool；容器要写指针形式。
#[test]
fn value_copy_of_container_requires_pointer_form() {
    assert_err(
        "valuecopy-container",
        "rwfunc take(int64·int64 #p) -> (int64 #r) {\n\t1 -> r\n}\n",
        "值复制参数只能是字符串、数值或 bool",
    );
}

// ── 写槽：参数槽本身永远不可写 ────────────────────────────────────

/// 错误 6：裸写指针形参 = 改指针绑定，不是写目标。
#[test]
fn bare_write_to_pointer_parameter_is_rejected() {
    assert_err(
        "bare-write",
        "rwfunc f(*[]int64 #arr) -> (*[]int64 #acc) {\n\tscatterarray·append(*arr, 1) -> acc\n}\n",
        "写指针形参必须显式解引用",
    );
}

/// 正例：显式解引用写目标 `-> *acc` 合法。
#[test]
fn explicit_deref_write_target_is_allowed() {
    assert_ok(
        "deref-write",
        "rwfunc f(*[]int64 #arr) -> (*[]int64 #acc) {\n\tscatterarray·append(*arr, 1) -> *acc\n}\n",
    );
}

// ── 空 kindexpr 与值复制参数（spec 03-类型系统/07-文法与合法性:61）──────

/// 错误 7：无类型、无 `*` 的形参——值复制没有值可拷贝，形态未定义。
#[test]
fn untyped_value_copy_parameter_is_rejected() {
    assert_err(
        "untyped-value-copy",
        "rwfunc f(#a) -> () {\n\t1 -> x\n}\n",
        "参数必须声明类型",
    );
}

/// 省略 `#name` 合法：参数定义键不存名字，所以省略不是「缺一半」，是完整形态。
#[test]
fn unnamed_parameters_are_accepted() {
    assert_ok("unnamed-param", "rwir op(*int64) -> (*int64)\n");
}

/// `#` 的终止符是**分隔符集合**，不是只认空格：`#n,` 的逗号必须留在注释之外，
/// 否则参数列表会被吞掉（表现为一串「expected RPAREN」级联错误）。
#[test]
fn hash_comment_stops_at_delimiters_not_only_spaces() {
    assert_ok(
        "hash-delimiter",
        "rwfunc f(*int64 #n, *int64 #m) -> (*int64 #r) {\n\t*r = *n + *m\n}\n",
    );
}

/// `inplace` 写在类型之前；写在名字之后不再接受，错误文本要指认正确位置。
#[test]
fn inplace_after_the_name_is_rejected() {
    assert_err(
        "inplace-after-name",
        "rwfunc f(*int64 #n) -> (*int64 #r inplace) {\n\t*r = *r + *n\n}\n",
        "`inplace` 写在参数类型之前",
    );
}

/// 错误 8：解引用值复制形参——它是只读副本，没有可写的目标。
#[test]
fn dereferencing_value_copy_parameter_is_rejected() {
    assert_err(
        "deref-value-copy",
        "rwfunc f(int64 #a) -> () {\n\tsub(*a) -> x\n}\n",
        "值复制参数不可解引用",
    );
}

/// 反例：`* #a` 是「不约束目标类型的指针形参」，体内写 `*a`——仍然缺类型。
#[test]
fn unconstrained_pointer_parameter_is_rejected() {
    // spec 05-runtime语义/04-函数调用:27：「`#a` 无类型、`* #a`、`a:` 都是错误」。
    // 参数是**绑定**不是值类型表达式——空 kindexp 在值类型层面合法（Ptr 目标可空），
    // 但参数声明没有类型就没有可绑定的契约。
    assert_err(
        "empty-kindexpr-ptr",
        "rwfunc f(* #a) -> () {\n\t*a + 1 -> x\n}\n",
        "参数必须声明类型",
    );
}

// ── 写位置的显式解引用（`*` 在任何位置都不得省略）──────────────────

/// 错误 9：写参的成员写同样要显式解引用——写位置不给例外。
#[test]
fn implicit_deref_on_write_param_member_is_rejected() {
    assert_err(
        "implicit-write-member",
        "struct N {\n\tval:int64=0\n}\nrwfunc f() -> (*N #r) {\n\tr·val = 5\n}\n",
        "对指针类型的成员访问必须显式解引用",
    );
}

/// 正例：`(*r)·val = 5` 是写参成员写的正确形态。
#[test]
fn explicit_deref_write_member_is_allowed() {
    assert_ok(
        "deref-write-member",
        "struct N {\n\tval:int64=0\n}\nrwfunc f() -> (*N #r) {\n\t(*r)·val = 5\n}\n",
    );
}

/// 正例：动态键写 `-> (*m)·*k`——`·` 后的 `*k` 是键，不是乘号。
#[test]
fn explicit_deref_dynamic_key_write_is_allowed() {
    assert_ok(
        "deref-dynamic-key",
        "rwfunc f(*int64 #v) -> (*int64·int64 #m) {\n\t*v -> (*m)·*k\n}\n",
    );
}

// ── 指针与值不同型（spec 16-ptr：`p` 得地址值、`*p` 得值）──────────

/// `root`（`*N`）写进 `[int64]·*N`：指针赋指针，成立。
#[test]
fn pointer_assigns_to_pointer_slot() {
    assert_ok(
        "ptr-to-ptr",
        "struct N {\n\tleft:*N=None\n}\nrwfunc f(*N #root) -> () {\n\tmp:[int64]·*N = {}\n\troot -> mp·1\n}\n",
    );
}

/// `root`（`*N`）写进 `[int64]·N`：值槽收不下指针，必须拒。
#[test]
fn pointer_does_not_assign_to_value_slot() {
    assert_err(
        "ptr-to-value",
        "struct N {\n\tleft:*N=None\n}\nrwfunc f(*N #root) -> () {\n\tmv:[int64]·N = {}\n\troot -> mv·1\n}\n",
        "cannot assign",
    );
}

/// `*root`（`N`）写进 `[int64]·N`：值赋值值，成立。
#[test]
fn deref_assigns_to_value_slot() {
    assert_ok(
        "deref-to-value",
        "struct N {\n\tleft:*N=None\n}\nrwfunc f(*N #root) -> () {\n\tmv:[int64]·N = {}\n\t*root -> mv·1\n}\n",
    );
}

/// `*root`（`N`）写进 `[int64]·*N`：值槽填不进指针槽，必须拒。
#[test]
fn deref_does_not_assign_to_pointer_slot() {
    assert_err(
        "deref-to-ptr",
        "struct N {\n\tleft:*N=None\n}\nrwfunc f(*N #root) -> () {\n\tmp:[int64]·*N = {}\n\t*root -> mp·1\n}\n",
        "cannot assign",
    );
}

// ── 并集加括号：`*(A|B)` 作用于整个类型表达式 ──────────────────────

/// 正例：`*(A|B)` 是「指向 A|B 的 kvlang 指针」，与逐成员写法区分开。
#[test]
fn parenthesized_union_pointer_is_allowed() {
    // `rwfunc` 参数禁止并集（2026-11-05）后，这条语法规则改由 `rwir` 声明承载：
    // 括号包住的并集仍是**一个**类型表达式，`*` 作用于它整体，而不是逐成员各加一个 `*`。
    assert_ok(
        "union-paren",
        "rwir f(*(int64|float64) #x) -> (*int64 #r)\n",
    );
}

/// 错误：逐成员写 `*A|*B`——同一参数两种传递方式。
#[test]
fn per_member_pointer_union_is_rejected() {
    assert_err(
        "union-per-member",
        "rwfunc f(*int64|*float64 #x) -> (int64 #r) {\n\t1 -> r\n}\n",
        "`*` 作用于整个类型表达式",
    );
}

// ── 解引用基座的下标写（`(*p)[i] = v`）────────────────────────────

/// 常量下标：`(*r)[0] = 72` 落元素写。
#[test]
fn deref_subscript_write_with_constant_index() {
    assert_ok(
        "deref-sub-const",
        "rwfunc t() -> (*[]char/utf32 #r) {\n\t(*r)[0] = 72\n}\n",
    );
}

/// 变量下标：`(*r)[*i] = 72`——`*i` 是下标的**取值**，不是另一个写目标。
#[test]
fn deref_subscript_write_with_variable_index() {
    assert_ok(
        "deref-sub-var",
        "rwfunc t(*int64 #i) -> (*[]char/utf32 #r) {\n\t(*r)[*i] = 72\n}\n",
    );
}

/// 局部变量下标：`i = 0; (*r)[i] = 72`。
#[test]
fn deref_subscript_write_with_local_index() {
    assert_ok(
        "deref-sub-local",
        "rwfunc t() -> (*[]char/utf32 #r) {\n\ti = 0\n\t(*r)[i] = 72\n}\n",
    );
}

/// 反向控制：裸指针基座的下标写仍是隐式解引用。
#[test]
fn implicit_deref_subscript_write_is_rejected() {
    assert_err(
        "implicit-sub-write",
        "rwfunc t(*int64 #i) -> (*[]char/utf32 #r) {\n\tr[*i] = 72\n}\n",
        "对指针类型的坐标访问必须显式解引用",
    );
}

// ── 多重 kvlang 指针：`**T` 的值就是 `*T` ──────────────────────────

/// 写参 `**N #h`：`*h` 的值是 `*N`，可接收取址结果。
#[test]
fn double_pointer_write_param_accepts_address() {
    assert_ok(
        "dbl-ptr-write",
        "struct N {\n\tv:int64=0\n}\nrwfunc c() -> (**N #h) {\n\tg:N = {v=2}\n\t&g -> *h\n}\n",
    );
}

/// 读参 `**N #h`：`*h` 读出的是 `*N`，可赋给局部再解引用——与写侧同型。
#[test]
fn double_pointer_read_param_yields_pointer_value() {
    assert_ok(
        "dbl-ptr-read",
        "struct N {\n\tv:int64=0\n}\nrwfunc r(**N #h) -> (*int64 #o) {\n\tcur = *h\n\t(*cur)·v -> *o\n}\n",
    );
}

/// 单指针写参 `*N #h`：`*h` 的值是 `N`，收不下指针。
#[test]
fn single_pointer_write_param_rejects_address() {
    assert_err(
        "single-ptr-write",
        "struct N {\n\tv:int64=0\n}\nrwfunc c() -> (*N #h) {\n\tg:N = {v=2}\n\t&g -> *h\n}\n",
        "cannot assign",
    );
}

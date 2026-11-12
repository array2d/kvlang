
use std::path::PathBuf;
use std::process::Command;

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
    
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    TempDir(dir)
}

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

#[test]
fn implicit_deref_on_member_access_is_rejected() {
    assert_err(
        "implicit-member",
        "struct Point {\n\tx:int64=0\n\ty:int64=0\n}\n\
         rwfunc read(*Point #p) -> (int64 #r) {\n\tp·x -> r\n}\n",
        "对指针类型的成员访问必须显式解引用",
    );
}

#[test]
fn implicit_deref_on_coordinate_access_is_rejected() {
    assert_err(
        "implicit-coord",
        "rwfunc first(*int64·int64 #p) -> (int64 #r) {\n\tp·[1] -> r\n}\n",
        "对指针类型的坐标访问必须显式解引用",
    );
}

#[test]
fn old_parameter_syntax_is_rejected() {
    assert_err(
        "oldsyntax",
        "rwfunc add_one(n:int64) -> (r:int64) {\n\tn + 1 -> r\n}\n",
        "参数声明必须是 `*T #name` 或 `T #name`",
    );
}

#[test]
fn value_copy_of_container_requires_pointer_form() {
    assert_err(
        "valuecopy-container",
        "rwfunc take(int64·int64 #p) -> (int64 #r) {\n\t1 -> r\n}\n",
        "值复制参数只能是字符串、数值或 bool",
    );
}

#[test]
fn bare_write_to_pointer_parameter_is_rejected() {
    assert_err(
        "bare-write",
        "rwfunc f(*[]int64 #arr) -> (*[]int64 #acc) {\n\tscatterarray·append(*arr, 1) -> acc\n}\n",
        "写指针形参必须显式解引用",
    );
}

#[test]
fn explicit_deref_write_target_is_allowed() {
    assert_ok(
        "deref-write",
        "rwfunc f(*[]int64 #arr) -> (*[]int64 #acc) {\n\tscatterarray·append(*arr, 1) -> *acc\n}\n",
    );
}

#[test]
fn untyped_value_copy_parameter_is_rejected() {
    assert_err(
        "untyped-value-copy",
        "rwfunc f(#a) -> () {\n\t1 -> x\n}\n",
        "参数必须声明类型",
    );
}

#[test]
fn unnamed_parameters_are_accepted() {
    assert_ok("unnamed-param", "rwir op(*int64) -> (*int64)\n");
}

#[test]
fn hash_comment_stops_at_delimiters_not_only_spaces() {
    assert_ok(
        "hash-delimiter",
        "rwfunc f(*int64 #n, *int64 #m) -> (*int64 #r) {\n\t*r = *n + *m\n}\n",
    );
}

#[test]
fn inplace_after_the_name_is_rejected() {
    assert_err(
        "inplace-after-name",
        "rwfunc f(*int64 #n) -> (*int64 #r inplace) {\n\t*r = *r + *n\n}\n",
        "`inplace` 写在参数类型之前",
    );
}

#[test]
fn dereferencing_value_copy_parameter_is_rejected() {
    assert_err(
        "deref-value-copy",
        "rwfunc f(int64 #a) -> () {\n\tsub(*a) -> x\n}\n",
        "值复制参数不可解引用",
    );
}

#[test]
fn unconstrained_pointer_parameter_is_rejected() {
    
    assert_err(
        "empty-kindexpr-ptr",
        "rwfunc f(* #a) -> () {\n\t*a + 1 -> x\n}\n",
        "参数必须声明类型",
    );
}

#[test]
fn implicit_deref_on_write_param_member_is_rejected() {
    assert_err(
        "implicit-write-member",
        "struct N {\n\tval:int64=0\n}\nrwfunc f() -> (*N #r) {\n\tr·val = 5\n}\n",
        "对指针类型的成员访问必须显式解引用",
    );
}

#[test]
fn explicit_deref_write_member_is_allowed() {
    assert_ok(
        "deref-write-member",
        "struct N {\n\tval:int64=0\n}\nrwfunc f() -> (*N #r) {\n\t(*r)·val = 5\n}\n",
    );
}

#[test]
fn parenthesized_union_pointer_is_allowed() {
    
    assert_ok(
        "union-paren",
        "rwir f(*(int64|float64) #x) -> (*int64 #r)\n",
    );
}

#[test]
fn per_member_pointer_union_is_rejected() {
    assert_err(
        "union-per-member",
        "rwfunc f(*int64|*float64 #x) -> (int64 #r) {\n\t1 -> r\n}\n",
        "`*` 作用于整个类型表达式",
    );
}

#[test]
fn deref_subscript_write_with_constant_index() {
    assert_ok(
        "deref-sub-const",
        "rwfunc t() -> (*[]char/utf32 #r) {\n\t(*r)[0] = 72\n}\n",
    );
}

#[test]
fn deref_subscript_write_with_variable_index() {
    assert_ok(
        "deref-sub-var",
        "rwfunc t(*int64 #i) -> (*[]char/utf32 #r) {\n\t(*r)[*i] = 72\n}\n",
    );
}

#[test]
fn implicit_deref_subscript_write_is_rejected() {
    assert_err(
        "implicit-sub-write",
        "rwfunc t(*int64 #i) -> (*[]char/utf32 #r) {\n\tr[*i] = 72\n}\n",
        "对指针类型的坐标访问必须显式解引用",
    );
}

#[test]
fn double_pointer_write_param_accepts_address() {
    assert_ok(
        "dbl-ptr-write",
        "struct N {\n\tv:int64=0\n}\nrwfunc c() -> (**N #h) {\n\tg:N = {v=2}\n\t&g -> *h\n}\n",
    );
}

#[test]
fn single_pointer_write_param_rejects_address() {
    assert_err(
        "single-ptr-write",
        "struct N {\n\tv:int64=0\n}\nrwfunc c() -> (*N #h) {\n\tg:N = {v=2}\n\t&g -> *h\n}\n",
        "cannot assign",
    );
}

#[test]
fn empty_comment_after_hash_is_rejected() {
    assert_err(
        "empty-comment",
        "rwfunc f(*int64 #x) -> (*int64 #R) {\n\t*x + 1 -> *R   # 这行是注释，不该变成指令\n}\n",
        "空注释",
    );
}

#[test]
fn comment_without_space_still_compiles() {
    assert_ok(
        "nonempty-comment",
        "rwfunc f(*int64 #x) -> (*int64 #R) {\n\t*x + 1 -> *R\n}\n",
    );
}

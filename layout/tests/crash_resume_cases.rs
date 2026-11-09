//! crash/resume verdicts for the review repros (`build/claude-1108/rev-a/cases`).

mod support;

use kvlanglayout::{compile, init_dirs, kvkind, Kv};
use std::sync::atomic::{AtomicUsize, Ordering};
use support::TempDirectory;

static SEQ: AtomicUsize = AtomicUsize::new(0);

const CAPABILITIES: &str = "rwir println(*[]char/utf32 $s...)\n";

/// Lay `src` out into a fresh store; return the record of each function in `names`.
fn verdicts(src: &str, names: &[&str]) -> Vec<String> {
    let dir = TempDirectory::new(&format!(
        "crash_resume_cases_{}",
        SEQ.fetch_add(1, Ordering::SeqCst)
    ));
    let mut kv = Kv::conn(&dir.dsn());
    init_dirs(&mut kv).unwrap();
    compile(&mut kv, CAPABILITIES).unwrap();
    compile(&mut kv, src).unwrap();
    names
        .iter()
        .map(|n| kvkind::value_string(&kv.get_one(&format!("/lib/{n}/\u{2025}crash_resume"))))
        .collect()
}

/// `vthread·call` / `create` / `run` are opaque even though they are declared rwirs (was `3<-0`).
#[test]
fn vthread_ops_are_never_replayed() {
    let src = "rwfunc greet() -> () {\n\t/tmp/rev_a_g -> t:int64\n\tt + 1 -> /tmp/rev_a_g\n}\nrwfunc test() -> () {\n\tx:int64 = 0\n\tname:[]char/utf32 = \"greet\"\n\tvthread·call(name)\n\tx = x + 1\n\tx -> /tmp/rev_a_x\n}\nrwfunc test2() -> () {\n\tx:int64 = 0\n\tvthread·create(\"greet\") -> vid:char/utf8\n\tvthread·run(vid)\n\tx = x + 1\n\tx -> /tmp/rev_a_x\n}\n";
    assert_eq!(
        verdicts(src, &["test", "test2"]),
        [
            "NOT_RESUMABLE:2:callee:vthread·call",
            "NOT_RESUMABLE:1:callee:vthread·create"
        ]
    );
}

/// A replay range may not re-roll `random·intn` (was `5<-2`).
#[test]
fn volatile_result_blocks_the_range() {
    let src = "rwfunc pick(uint64 $k) -> (*int64 $x, *int64 $y) {\n\tif (k == 0) {\n\t\t1 -> *x\n\t} else {\n\t\t1 -> *y\n\t}\n}\nrwfunc f() -> () {\n\ta:int64 = 0\n\tb:int64 = 0\n\ts:int64 = 0\n\tk:uint64 = random·intn(2)\n\tpick(k) -> a, b\n\ts = s + 1\n\ta -> /tmp/rev_a_a\n\tb -> /tmp/rev_a_b\n}\n";
    assert_eq!(
        verdicts(src, &["f"]),
        ["NOT_RESUMABLE:5:volatile:random·intn"]
    );
}

/// Same with a random dynamic key.
#[test]
fn volatile_key_blocks_the_range() {
    let src = "rwfunc f() -> () {\n\tx:int64 = 0\n\tm:'[]char/utf8'int64 = {}\n\tk:uint64 = random·intn(100)\n\t1 -> m·*k\n\tx = x + 1\n\tx -> /tmp/rev_a_x\n\tm -> /tmp/rev_a_m\n}\n";
    assert_eq!(
        verdicts(src, &["f"]),
        ["NOT_RESUMABLE:5:volatile:random·intn"]
    );
}

/// `map·new` keeps old members, so it is no kill (was `6<-2`).
#[test]
fn map_new_keeps_old_members() {
    let src = "rwfunc f() -> () {\n\tm:'[]char/utf8'int64 = {}\n\t5 -> m·b\n\tx:int64 = 0\n\tm:'[]char/utf8'int64 = {}\n\tm·b -> t:int64\n\tt + 1 -> m·b\n\tx = x + t\n\tx -> /tmp/rev_a_x\n}\n";
    assert_eq!(verdicts(src, &["f"]), ["NOT_RESUMABLE:6:/tmp/rev_a_x"]);
}

/// `_1 = *n; _1·v` reaches the parameter (was `3<-1`).
#[test]
fn member_of_lowering_temp_is_heap() {
    let src = "struct Node {\n\tv:int64=0\n}\nrwfunc g() -> (*/lib/Node $n) {\n\t(*n)·v -> t:int64\n\tt + 1 -> (*n)·v\n\tt = t + 1\n\tt -> /tmp/rev_a_t\n}\n";
    assert_eq!(verdicts(src, &["g"]), ["NOT_RESUMABLE:2:/tmp/rev_a_t"]);
}

/// `m2 = m1` binds m2 to m1's path (was `7<-4`).
#[test]
fn container_copy_binds_an_alias() {
    let src = "rwfunc f() -> () {\n\tm1:'[]char/utf8'int64 = {}\n\t5 -> m1·b\n\tm2:'[]char/utf8'int64 = {}\n\tm2 = m1\n\tx:int64 = 0\n\tm1·b -> t:int64\n\tt + 1 -> m2·b\n\tx = x + t\n\tx -> /tmp/rev_a_x\n}\n";
    assert_eq!(verdicts(src, &["f"]), ["NOT_RESUMABLE:7:/tmp/rev_a_x"]);
}

/// Same through a copy into a non-empty map.
#[test]
fn map_copy_binds_an_alias() {
    let src = "struct P {\n\tv:int64=0\n\tw:int64=0\n}\nrwfunc f() -> () {\n\tm1:'[]char/utf8'int64 = {}\n\tm2:'[]char/utf8'int64 = {}\n\t5 -> m2·b\n\tx:int64 = 0\n\tm2 = m1\n\tm2·b -> t:int64\n\tt + 1 -> m2·b\n\tx = x + t\n\tp:P = {}\n\tq:P = {v=1}\n\tp = q\n\tx -> /tmp/rev_a_x\n}\n";
    assert_eq!(verdicts(src, &["f"]), ["NOT_RESUMABLE:7:/tmp/rev_a_x"]);
}

/// `p·k` with a path-valued string reaches `/tmp/rev_a_x·k` (was `4<-1`).
#[test]
fn path_string_base_reaches_absolute_key() {
    let src = "rwfunc f() -> () {\n\tp:[]char/utf8 = \"/tmp/rev_a_x\"\n\tx:int64 = 0\n\t/tmp/rev_a_x·k -> t:int64\n\tt + 1 -> p·k\n\tx = x + t\n\tx -> /tmp/rev_a_out\n}\n";
    assert_eq!(verdicts(src, &["f"]), ["NOT_RESUMABLE:4:/tmp/rev_a_out"]);
}

/// `&a` of a by-value param writes caller storage (was `4<-1`).
#[test]
fn address_of_by_value_param() {
    let src = "rwfunc f(int64 $a) -> () {\n\t&a -> p:*int64\n\tx:int64 = 0\n\ta -> t:int64\n\t5 -> *p\n\tx = x + t\n\tx -> /tmp/rev_a_x\n}\n";
    assert_eq!(verdicts(src, &["f"]), ["CONDITIONALLY_RESUMABLE:4<-caller"]);
}

/// `leak(v)` may keep v's address; `*p` then writes v (was `6<-3`).
#[test]
fn escape_through_callee() {
    let src = "rwfunc leak(*int64 $x) -> (**int64 $out) {\n\tx -> *out\n}\nrwfunc f() -> () {\n\tv:int64 = 1\n\tp:*int64 = None\n\tleak(v) -> p\n\ts:int64 = 0\n\tv -> t:int64\n\t5 -> *p\n\ts = s + t\n\ts -> /tmp/rev_a_s\n}\n";
    assert_eq!(
        verdicts(src, &["leak", "f"]),
        [
            "CONDITIONALLY_RESUMABLE:0<-caller",
            "CONDITIONALLY_RESUMABLE:5<-entry,6<-entry"
        ]
    );
}

/// `f(v) -> v` binds both params to v (was `4<-1`).
#[test]
fn by_value_param_aliases_write_param() {
    let src = "rwfunc f(int64 $a) -> (*int64 $r) {\n\ta -> *r\n\tx:int64 = 0\n\ta -> t:int64\n\tt + 1 -> *r\n\tx = x + t\n\tx -> /tmp/rev_a_x\n}\nrwfunc h() -> () {\n\t5 -> v:int64\n\tf(v) -> v\n\tprintln(v)\n}\n";
    assert_eq!(
        verdicts(src, &["f", "h"]),
        [
            "CONDITIONALLY_RESUMABLE:0<-caller,4<-caller",
            "CONDITIONALLY_RESUMABLE:1<-0"
        ]
    );
}

/// `f() -> /tmp/rev_a_g` while f reads that key: P2 checked inside the unit.
#[test]
fn same_unit_caller_binds_param_to_absolute_key() {
    let src = "rwfunc f() -> (*int64 $r) {\n\tx:int64 = 0\n\t/tmp/rev_a_g -> t:int64\n\tt + 1 -> *r\n\tx = x + t\n\tx -> /tmp/rev_a_x\n}\nrwfunc h() -> () {\n\tf() -> /tmp/rev_a_g\n}\n";
    assert_eq!(
        verdicts(src, &["f", "h"]),
        ["NOT_RESUMABLE:3:/tmp/rev_a_g", "NOT_RESUMABLE:0:callee:f"]
    );
}

/// Replay from the kill of x re-establishes x before `*q` writes it.
#[test]
fn pointer_copy_keeps_kill_of_target() {
    let src = "rwfunc f(int64 $n) -> (*int64 $r) {\n\ts:int64 = 0\n\ti:int64 = 0\n\twhile (i < n) {\n\t\ti = i + 1\n\t\tif (i == 2) {\n\t\t\tcontinue\n\t\t}\n\t\tif (i == 5) {\n\t\t\tbreak\n\t\t}\n\t\ts = s + i\n\t}\n\ts -> *r\n}\nrwfunc g() -> () {\n\tx:int64 = 1\n\t&x -> p:*int64\n\tq:*int64 = p\n\ty:int64 = 0\n\tx -> t:int64\n\t9 -> *q\n\ty = y + t\n\ty -> /tmp/rev_a_y\n}\n";
    assert_eq!(verdicts(src, &["g"]), ["CONDITIONALLY_RESUMABLE:6<-0"]);
}

// ── round 3 repros (`build/claude-1108/rev-c/cases`): op table and read-param writes ──

/// `compactarray·set(a, 0, 9) -> b` patches a (was `5<-2`).
#[test]
fn array_set_patches_source_not_target() {
    let src = "rwfunc test() -> () {\n\ta:[]int64 = [1, 2, 3]\n\tb:[]int64 = [0, 0, 0]\n\tx:int64 = 0\n\tt:int64 = a[0]\n\tcompactarray·set(a, 0, 9) -> b\n\tx = x + t\n\tprintln(x)\n\tprintln(a)\n\tprintln(b)\n}\n";
    assert_eq!(
        verdicts(src, &["test"]),
        ["CONDITIONALLY_RESUMABLE:4<-entry,5<-entry"]
    );
}

/// `string·set` on utf32 patches the write slot, so it is no kill (was `6<-2`); the copies
/// around it may meet a rebound size, so no range survives.
#[test]
fn utf32_string_set_is_a_partial_write() {
    let src = "rwfunc test() -> () {\n\ts:[]char/utf32 = \"abc\"\n\tt:[]char/utf32 = \"xyz\"\n\tx:int64 = 0\n\tstring·set(s, 0, \"q\") -> t\n\tu:[]char/utf32 = t\n\tt = \"mno\"\n\tx = x + 1\n\tprintln(u)\n}\n";
    assert_eq!(
        verdicts(src, &["test"]),
        ["CONDITIONALLY_RESUMABLE:3<-entry,6<-entry"]
    );
}

/// The callee patches its read param, so the caller's argument is written (was `5<-2`).
#[test]
fn callee_patches_pointer_read_param() {
    let src = "rwfunc poke(*[]int64 $a) -> (*[]int64 $r) {\n\tcompactarray·set(*a, 0, 9) -> *r\n}\nrwfunc test() -> () {\n\ta:[]int64 = [1, 2, 3]\n\tb:[]int64 = [0, 0, 0]\n\tx:int64 = 0\n\tt:int64 = a[0]\n\tpoke(a) -> b\n\tx = x + t\n\tprintln(x)\n}\n";
    assert_eq!(
        verdicts(src, &["poke", "test"]),
        [
            "CONDITIONALLY_RESUMABLE:1<-caller",
            "CONDITIONALLY_RESUMABLE:4<-entry,5<-entry"
        ]
    );
}

/// `kvspace·listat` may leave the slot alone (was `5<-1`); the reassigned copy blocks a
/// range from step 0, and `*out` makes the fallback `caller`.
#[test]
fn listat_absent_entry_is_no_kill() {
    let src = "rwfunc f() -> (*bool $out) {\n\tname:[]char/utf8 = \"init\"\n\tx:int64 = 0\n\tkvspace·listat(/tmp/rc_dir, 99) -> name\n\tu:[]char/utf8 = name\n\tname = \"zzzz\"\n\tx = x + 1\n\tu == \"init\" -> *out\n}\nrwfunc test() -> () {\n\n\tf() -> r:bool\n\tif (r) {\n\t\tprintln(\"u is init (correct)\")\n\t} else {\n\t\tprintln(\"u is NOT init (wrong)\")\n\t}\n}\n";
    assert_eq!(
        verdicts(src, &["f"]),
        ["CONDITIONALLY_RESUMABLE:2<-0,5<-caller"]
    );
}

/// A by-value read param is bound by address too (caller was `5<-2`).
#[test]
fn callee_patches_by_value_read_param() {
    let src = "rwfunc poke([]int64 $a) -> (*[]int64 $r) {\n\tcompactarray·set(a, 0, 9) -> *r\n}\nrwfunc test() -> () {\n\ta:[]int64 = [1, 2, 3]\n\tb:[]int64 = [0, 0, 0]\n\tx:int64 = 0\n\tt:int64 = a[0]\n\tpoke(a) -> b\n\tx = x + t\n\tu:int64 = a[0]\n\tif (x == 1) {\n\t\tprintln(\"x == 1 (correct)\")\n\t} else {\n\t\tprintln(\"x != 1 (wrong)\")\n\t}\n\tif (u == 9) {\n\t\tprintln(\"a[0] == 9: callee wrote its read param\")\n\t}\n}\n";
    assert_eq!(
        verdicts(src, &["poke", "test"]),
        [
            "CONDITIONALLY_RESUMABLE:0<-caller",
            "CONDITIONALLY_RESUMABLE:4<-entry,5<-entry"
        ]
    );
}

/// `compactarray·at` copies 1 byte into a 1-4 byte `char/utf8` slot: not a kill (was `9<-6`).
#[test]
fn element_load_into_utf8_char_is_partial() {
    let src = "rwfunc test() -> () {\n\ta1:char/utf8 = 'a'\n\ta2:char/utf8 = 'b'\n\tarr:[2]char/utf8 = [a1, a2]\n\te1:char/utf8 = '\u{e9}'\n\tn1:char/utf8 = '\u{f1}'\n\tc:char/utf8 = e1\n\tc = arr[0]\n\td:char/utf8 = c\n\tc = n1\n\td = d\n}\n";
    assert_eq!(
        verdicts(src, &["test"]),
        ["CONDITIONALLY_RESUMABLE:6<-entry,9<-entry"]
    );
}

/// A member write through a write param reads the param's value (caller was `6<-3`).
#[test]
fn member_write_through_write_param_reads_it() {
    let src = "rwfunc g() -> (*'[]char/utf8'int64 $r) {\n\t5 -> (*r)\u{b7}k\n}\nrwfunc f() -> () {\n\tm1:'[]char/utf8'int64 = {}\n\tv:'[]char/utf8'int64 = {}\n\tv = m1\n\tx:int64 = 1\n\tg() -> v\n\tv:'[]char/utf8'int64 = {}\n\tx = x + 1\n\tprintln(x)\n}\n";
    assert_eq!(
        verdicts(src, &["g", "f"]),
        ["CONDITIONALLY_RESUMABLE:0<-caller", "NOT_RESUMABLE:4:heap"]
    );
}

/// An unverified rwir may touch any storage, even with literal-only operands (was ALWAYS).
#[test]
fn unverified_rwir_touches_storage() {
    let src = "rwir bump() -> ()\nrwir bump2(int64 $n) -> ()\nrwfunc f() -> () {\n\tbump()\n\tbump2(5)\n\t5 -> /tmp/revd_x\n}\n";
    assert_eq!(verdicts(src, &["f"]), ["NOT_RESUMABLE:0:unverified:bump"]);
}

// ── round 4 repros (`build/claude-1108/rev-e/cases`): slot size ─────────────

/// `c = e1` stores in place; `c = string·char(s, 0)` rebinds c at 1 byte (was `5<-0`).
#[test]
fn create_resizes_a_slot_an_earlier_copy_stores_into() {
    let src = "rwfunc test() -> () {\n\tx:int64 = 0\n\ts:[]char/utf8 = \"a\"\n\te1:char/utf8 = 'é'\n\tc:char/utf8 = e1\n\tc = string·char(s, 0)\n\tx = x + 1\n\tx -> /tmp/reve_x\n\tc -> /tmp/reve_c\n}\n";
    assert_eq!(
        verdicts(src, &["test"]),
        ["CONDITIONALLY_RESUMABLE:5<-entry"]
    );
}

/// Replaying the `"abc"` copy into the 2-byte slice fails (was `1<-0`).
#[test]
fn slice_resizes_its_source() {
    let src = "rwfunc test() -> () {\n\ts:[]char/utf8 = \"abc\"\n\ts = string·slice(s, 0, 2)\n\ts -> /tmp/reve_s\n}\n";
    assert_eq!(
        verdicts(src, &["test"]),
        ["CONDITIONALLY_RESUMABLE:1<-entry"]
    );
}

/// `p = q` stores the path in place; `&bbbbbbbb -> p` rebinds p longer (was `6<-0`).
#[test]
fn pointer_copy_then_address_of_resizes() {
    let src = "rwfunc test() -> () {\n\tx:int64 = 0\n\ta:int64 = 1\n\tbbbbbbbb:int64 = 2\n\t&a -> q:*int64\n\tp:*int64 = q\n\t&bbbbbbbb -> p\n\tx = x + 1\n\tx -> /tmp/reve_x\n}\n";
    assert_eq!(
        verdicts(src, &["test"]),
        ["CONDITIONALLY_RESUMABLE:6<-entry"]
    );
}

/// g stores `*r` in place; the caller later rebinds v by a slice (was `5<-2`). Step 1 is a
/// sound anchor: its slice rebinds v at the size g's store expects.
#[test]
fn callee_store_into_write_param_needs_its_size() {
    let src = "rwfunc g() -> (*[]char/utf8 $r) {\n\tt:[]char/utf8 = \"abc\"\n\tt -> *r\n}\nrwfunc test() -> () {\n\ts:[]char/utf8 = \"abc\"\n\tv:[]char/utf8 = string·slice(s, 0, 12)\n\tx:int64 = 0\n\tg() -> v\n\tv = string·slice(s, 0, 2)\n\tx = x + 1\n\tx -> /tmp/reve_x\n\tv -> /tmp/reve_v\n}\n";
    assert_eq!(
        verdicts(src, &["g", "test"]),
        ["ALWAYS_RESUMABLE", "CONDITIONALLY_RESUMABLE:5<-1"]
    );
}

/// An in-place store into an absolute key may rebind it (create_static_target), so a later
/// size change of that key blocks the range (were `6<-3` and `7<-4`).
#[test]
fn inplace_store_into_absolute_key_may_rebind() {
    let h2 = "rwfunc test() -> () {\n\tarr:[]int64 = [1, 2, 3]\n\t/tmp/c:[]char/utf8 = \"zzz\"\n\t&/tmp/c -> p:*[]char/utf8\n\tx:int64 = 0\n\t\"yyy\" -> *p\n\tcompactarray·langtype(arr) -> /tmp/c\n\tx = x + 1\n\tx -> /tmp/x\n}\n";
    let h3 = "rwfunc test() -> () {\n\tb:[]int64 = [7, 8]\n\tc:[]int64 = [4, 5, 6]\n\t/tmp/a:[]int64 = [1, 2, 3]\n\t&/tmp/a -> p:*[]int64\n\tx:int64 = 0\n\tc -> *p\n\tcompactarray·copy(b) -> /tmp/a\n\tx = x + 1\n\tx -> /tmp/x\n}\n";
    assert_eq!(verdicts(h2, &["test"]), ["NOT_RESUMABLE:6:/tmp/c"]);
    assert_eq!(verdicts(h3, &["test"]), ["NOT_RESUMABLE:7:/tmp/a"]);
}

/// Layout accepts a text result into a `char/utf32` slot, so char slots are not fixed width
/// (was `6<-2`).
#[test]
fn char_slot_may_hold_text() {
    let src = "rwfunc test() -> () {\n\ts:[]char/utf32 = \"abcdef\"\n\tc:char/utf32 = 'a'\n\tx:int64 = 0\n\tc2:char/utf32 = 'b'\n\tc2 -> c\n\tstring·slice(s, 0, 2) -> c\n\tx = x + 1\n\tx -> /tmp/x\n\tc -> /tmp/c\n}\n";
    assert_eq!(verdicts(src, &["test"]), ["CONDITIONALLY_RESUMABLE:6<-entry"]);
}

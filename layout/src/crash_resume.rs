
use std::collections::{BTreeSet, HashMap};
use std::fmt;

const MEMBER: char = '\u{b7}';

#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Class {
    
    Local(String),
    Abs(String),
    
    Params,
    
    Heap,
    
    Bare,
    
    Size(Box<Class>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WriteMode {
    
    Whole,
    
    Element,
    
    Partial,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Store {
    
    InPlace,
    
    Create,
    
    Both,
    
    Untouched,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct OpSpec {
    
    pub patches: u32,
    pub write: WriteMode,
    pub store: Store,
    pub heap_read: bool,
    pub heap_write: bool,
    
    pub volatile: bool,
    
    pub opaque: bool,
}

const fn spec(write: WriteMode, store: Store) -> OpSpec {
    OpSpec {
        patches: 0,
        write,
        store,
        heap_read: false,
        heap_write: false,
        volatile: false,
        opaque: false,
    }
}

const WHOLE: OpSpec = spec(WriteMode::Whole, Store::InPlace);

const CREATE: OpSpec = spec(WriteMode::Whole, Store::Create);
const NO_WRITE: OpSpec = spec(WriteMode::Partial, Store::Untouched);
const HEAP_READ: OpSpec = OpSpec {
    heap_read: true,
    ..WHOLE
};
const HEAP_WRITE: OpSpec = OpSpec {
    heap_write: true,
    ..NO_WRITE
};
const HEAP_RW_CREATE: OpSpec = OpSpec {
    heap_read: true,
    heap_write: true,
    write: WriteMode::Partial,
    ..CREATE
};
const VOLATILE: OpSpec = OpSpec {
    volatile: true,
    ..WHOLE
};
const OPAQUE: OpSpec = OpSpec {
    opaque: true,
    ..spec(WriteMode::Partial, Store::Both)
};

pub const OP_TABLE: &[(&str, OpSpec)] = &[
    
    ("copy", spec(WriteMode::Whole, Store::Both)),
    
    ("add", WHOLE),
    ("sub", WHOLE),
    ("mul", WHOLE),
    ("div", WHOLE),
    ("mod", WHOLE),
    ("max", WHOLE),
    ("min", WHOLE),
    ("abs", WHOLE),
    ("neg", WHOLE),
    ("sign", WHOLE),
    ("bitand", WHOLE),
    ("bitor", WHOLE),
    ("bitxor", WHOLE),
    ("shl", WHOLE),
    ("shr", WHOLE),
    ("sqrt", WHOLE),
    ("exp", WHOLE),
    ("log", WHOLE),
    ("pow", WHOLE),
    ("eq", WHOLE),
    ("neq", WHOLE),
    ("lt", WHOLE),
    ("le", WHOLE),
    ("gt", WHOLE),
    ("ge", WHOLE),
    ("and", WHOLE),
    ("or", WHOLE),
    ("not", WHOLE),
    ("cast", WHOLE),
    ("concat", WHOLE),
    ("streq", WHOLE),
    ("streq·not", WHOLE),
    ("ptreq", WHOLE),
    ("ptreq·not", WHOLE),
    ("bool·eq", WHOLE),
    ("bool·neq", WHOLE),
    ("nonnull", WHOLE),
    ("nonnull·not", WHOLE),
    
    ("debugger", OPAQUE),
    
    ("compactarray·new", CREATE),
    ("compactarray·fill", CREATE),
    ("compactarray·newslice", CREATE),
    ("compactarray·newreshape", CREATE),
    ("compactarray·newreinterpret", CREATE),
    ("compactarray·copy", WHOLE),
    ("compactarray·numel", WHOLE),
    ("compactarray·dim", WHOLE),
    ("compactarray·bodylen", WHOLE),
    ("compactarray·langtype", WHOLE),
    
    ("compactarray·at", spec(WriteMode::Element, Store::InPlace)),
    
    (
        "compactarray·set",
        OpSpec {
            patches: 1,
            write: WriteMode::Partial,
            ..NO_WRITE
        },
    ),
    ("map·new", CREATE),
    
    (
        "struct·new",
        OpSpec {
            heap_write: true,
            ..CREATE
        },
    ),
    
    ("compactarray·parselangtype", WHOLE),
    ("xv·parselangtype", WHOLE),
    
    ("scatterarray·new", CREATE),
    ("scatterarray·len", HEAP_READ),
    (
        "scatterarray·at",
        OpSpec {
            heap_read: true,
            write: WriteMode::Partial,
            ..WHOLE
        },
    ),
    
    (
        "scatterarray·set",
        OpSpec {
            write: WriteMode::Partial,
            ..HEAP_WRITE
        },
    ),
    (
        "scatterarray·append",
        OpSpec {
            heap_read: true,
            write: WriteMode::Partial,
            ..HEAP_WRITE
        },
    ),
    ("scatterarray·newscatter", HEAP_RW_CREATE),
    ("scatterarray·newcompact", HEAP_RW_CREATE),
    ("scatterarray·slice", HEAP_RW_CREATE),
    
    ("string·set", spec(WriteMode::Partial, Store::Both)),
    ("string·char", CREATE),
    ("string·ord", WHOLE),
    ("string·cmp", WHOLE),
    ("string·find", WHOLE),
    ("string·len", WHOLE),
    ("string·slice", CREATE),
    ("string·concat", CREATE),
    ("string·formatint", CREATE),
    ("string·formatuint", CREATE),
    ("string·parseint", WHOLE),
    ("string·parseuint", WHOLE),
    ("string·parsefloat", WHOLE),
    
    (
        "time·now",
        OpSpec {
            volatile: true,
            ..CREATE
        },
    ),
    ("time·sub", WHOLE),
    ("time·add", WHOLE),
    ("time·before", WHOLE),
    ("time·after", WHOLE),
    ("time/duration·nanos", WHOLE),
    ("time/duration·millis", WHOLE),
    ("time/duration·seconds", WHOLE),
    ("time/duration·minutes", WHOLE),
    ("time/duration·hours", WHOLE),
    ("time/duration·as_nanos", WHOLE),
    ("time/duration·as_millis", WHOLE),
    ("time/duration·as_seconds", WHOLE),
    ("time/duration·as_minutes", WHOLE),
    ("time/duration·as_hours", WHOLE),
    ("time/duration·add", WHOLE),
    ("time/duration·sub", WHOLE),
    ("time/duration·before", WHOLE),
    ("time/duration·after", WHOLE),
    
    ("pathspace·del", HEAP_WRITE),
    ("pathspace·deltree", HEAP_WRITE),
    ("pathspace·mkindex", HEAP_WRITE),
    ("pathspace·extindex", HEAP_WRITE),
    ("pathspace·delextindex", HEAP_WRITE),
    (
        "pathspace·cp",
        OpSpec {
            heap_read: true,
            ..HEAP_WRITE
        },
    ),
    (
        "pathspace·cpdir",
        OpSpec {
            heap_read: true,
            ..HEAP_WRITE
        },
    ),
    (
        "pathspace·cplist",
        OpSpec {
            heap_read: true,
            ..HEAP_WRITE
        },
    ),
    ("pathspace·listlen", HEAP_READ),
    (
        "pathspace·listat",
        OpSpec {
            write: WriteMode::Partial,
            ..HEAP_READ
        },
    ),
    ("pathspace·watch", OPAQUE),
    ("pathspace/key·abs", CREATE),
    ("pathspace/key·tostr", CREATE),
    (
        "pathlang·resolvekey",
        OpSpec {
            heap_read: true,
            ..CREATE
        },
    ),
    ("random·uint64", VOLATILE),
    ("random·int63", VOLATILE),
    ("random·intn", VOLATILE),
    
    ("vthread·pc", VOLATILE),
    ("vthread·create", OPAQUE),
    ("vthread·run", OPAQUE),
    ("vthread·call", OPAQUE),
    ("vthread·setstatus", OPAQUE),
    
    ("vthread·sleep", NO_WRITE),
];

const OUTPUT_OPS: &[&str] = &["println", "print", "cerr"];

pub const fn unverified() -> OpSpec {
    OpSpec {
        patches: u32::MAX,
        volatile: true,
        heap_read: true,
        heap_write: true,
        ..spec(WriteMode::Partial, Store::Both)
    }
}

const CATALOG: &str = include_str!("../../runtimec/specification/rwir_catalog.def");
const ALIASES: &str = include_str!("../../runtimec/specification/rwir_alias_table.def");

fn def_row(line: &str, macro_name: &str) -> Option<(String, String)> {
    let rest = line.trim().strip_prefix(macro_name)?.strip_prefix('(')?;
    let open = rest.find('"')?;
    let close = open + 1 + rest[open + 1..].find('"')?;
    let quoted = rest[open + 1..close].to_string();
    let ident = rest
        .split(|c: char| !(c.is_ascii_alphanumeric() || c == '_'))
        .filter(|w| w.starts_with("RUNTIMEC_RWIR_"))
        .last()?
        .to_string();
    Some((quoted, ident))
}

fn surface_rows() -> &'static HashMap<String, Vec<String>> {
    static ROWS: std::sync::OnceLock<HashMap<String, Vec<String>>> = std::sync::OnceLock::new();
    ROWS.get_or_init(|| {
        let mut row_name: HashMap<String, String> = HashMap::new();
        for line in CATALOG.lines() {
            let t = line.trim();
            let Some(rest) = t.strip_prefix("RUNTIMEC_RWIR_OPERATION(") else {
                continue;
            };
            let Some((ident, tail)) = rest.split_once(',') else {
                continue;
            };
            if let Some(name) = tail.split('"').nth(1) {
                row_name.insert(ident.trim().to_string(), name.to_string());
            }
        }
        let mut rows: HashMap<String, Vec<String>> = HashMap::new();
        for line in ALIASES.lines() {
            let row = def_row(line, "RUNTIMEC_RWIR_ALIAS")
                .or_else(|| def_row(line, "RUNTIMEC_RWIR_SPECIALIZATION"));
            if let Some((surface, ident)) = row {
                if let Some(name) = row_name.get(&ident) {
                    rows.entry(surface).or_default().push(name.clone());
                }
            }
        }
        rows
    })
}

pub fn op_spec(name: &str) -> Option<OpSpec> {
    if OUTPUT_OPS.contains(&name) {
        return Some(NO_WRITE);
    }
    let rows = surface_rows().get(name)?;
    let mut merged: Option<OpSpec> = None;
    for row in rows {
        let (_, s) = OP_TABLE.iter().find(|(n, _)| n == row)?;
        merged = Some(match merged {
            None => *s,
            Some(m) => OpSpec {
                patches: m.patches | s.patches,
                write: if m.write == s.write {
                    m.write
                } else {
                    WriteMode::Partial
                },
                store: if m.store == s.store {
                    m.store
                } else {
                    Store::Both
                },
                heap_read: m.heap_read || s.heap_read,
                heap_write: m.heap_write || s.heap_write,
                volatile: m.volatile || s.volatile,
                opaque: m.opaque || s.opaque,
            },
        });
    }
    merged
}

fn scalar_type(t: &str) -> bool {
    crate::builtin::is_num_kind(t)
        || matches!(t, "bool" | "time" | "time/duration")
        || t.starts_with("char/")
}

pub fn killable_type(ty: &str) -> bool {
    let t = ty.trim();
    (scalar_type(t) && t != "char/utf8")
        || matches!(t, "[]char/utf8" | "[]char/utf32" | "[]char/ascii")
}

fn fixed_width_type(t: &str) -> bool {
    scalar_type(t) && !t.starts_with("char/")
}

pub fn value_type(ty: &str) -> bool {
    let t = ty.trim();
    if t.starts_with('*') {
        return true;
    }
    if t.contains(['|', '\'', MEMBER]) {
        return false;
    }
    killable_type(t)
        || t == "char/utf8"
        || t.strip_prefix('[')
            .and_then(|r| r.split_once(']'))
            .is_some_and(|(_, element)| scalar_type(element))
}

#[derive(Clone, Debug)]
pub enum Callee {
    
    None,
    
    Op(OpSpec),
    
    Unverified(String),
    
    Unit(usize),
    
    Store(String),
    Unknown(String),
}

pub struct StepInput {
    pub op: String,
    pub reads: Vec<String>,
    pub writes: Vec<String>,
    
    pub write_types: Vec<String>,
    pub callee: Callee,
}

pub struct ParamInfo {
    pub name: String,
    pub inplace: bool,
    
    pub value: bool,
}

pub struct FunctionInput {
    pub name: String,
    pub reads: Vec<ParamInfo>,
    pub writes: Vec<ParamInfo>,
    pub steps: Vec<StepInput>,
    
    pub label_starts: Vec<usize>,
    
    pub local_types: HashMap<String, String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Replay {
    From(usize),
    Entry,
    Caller,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Resumability {
    
    AlwaysResumable,
    
    ConditionallyResumable {
        points: Vec<(usize, Replay)>,
    },
    NotResumable {
        step: usize,
        reason: String,
    },
}

impl Resumability {
    pub fn name(&self) -> &'static str {
        match self {
            Resumability::AlwaysResumable => "ALWAYS_RESUMABLE",
            Resumability::ConditionallyResumable { .. } => "CONDITIONALLY_RESUMABLE",
            Resumability::NotResumable { .. } => "NOT_RESUMABLE",
        }
    }
}

impl fmt::Display for Resumability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Resumability::AlwaysResumable => write!(f, "ALWAYS_RESUMABLE"),
            Resumability::ConditionallyResumable { points } => {
                let parts: Vec<String> = points
                    .iter()
                    .map(|(c, r)| match r {
                        Replay::From(a) => format!("{c}<-{a}"),
                        Replay::Entry => format!("{c}<-entry"),
                        Replay::Caller => format!("{c}<-caller"),
                    })
                    .collect();
                write!(f, "CONDITIONALLY_RESUMABLE:{}", parts.join(","))
            }
            Resumability::NotResumable { step, reason } => {
                write!(f, "NOT_RESUMABLE:{step}:{reason}")
            }
        }
    }
}

struct Ctx<'a> {
    reads: &'a [ParamInfo],
    writes: &'a [ParamInfo],
    local_types: &'a HashMap<String, String>,
}

enum ParamRef {
    Read(usize),
    Write(usize),
}

struct Slot {
    
    classes: Vec<Class>,
    
    locate: Vec<Class>,
    
    bare: Option<Class>,
    
    wparam: Option<usize>,
    
    rparam: Option<usize>,
    
    local: Option<String>,
}

enum Operand {
    Ignore,
    Slot(Slot),
}

fn is_literal(t: &str) -> bool {
    matches!(t, "true" | "false" | "None")
        || t.trim_start_matches(['-', '+'])
            .starts_with(|c: char| c.is_ascii_digit())
}

fn is_ident(t: &str) -> bool {
    !t.is_empty()
        && !t.starts_with(|c: char| c.is_ascii_digit())
        && t.chars().all(|c| c == '_' || c.is_alphanumeric())
}

fn matching_paren(s: &str) -> Option<usize> {
    let mut depth = 0i32;
    for (i, c) in s.char_indices() {
        match c {
            '(' => depth += 1,
            ')' => {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
    }
    None
}

fn split_base(text: &str) -> Option<(usize, &str, &str)> {
    let mut depth = 0;
    let mut s = text.trim();
    loop {
        if let Some(rest) = s.strip_prefix('*') {
            depth += 1;
            s = rest.trim_start();
            continue;
        }
        if s.starts_with('(') {
            let close = matching_paren(s)?;
            let rest = &s[close + 1..];
            if rest.trim().is_empty() {
                s = s[1..close].trim();
                continue;
            }
            let (inner_depth, base, inner_tail) = split_base(&s[1..close])?;
            if !inner_tail.is_empty() {
                return None;
            }
            return Some((depth + inner_depth, base, rest));
        }
        break;
    }
    if s.starts_with('[') {
        let close = s.find(']')?;
        return Some((depth, &s[..=close], &s[close + 1..]));
    }
    let end = s.find(|c| c == MEMBER || c == '[').unwrap_or(s.len());
    Some((depth, &s[..end], &s[end..]))
}

fn parse_tail(tail: &str, ctx: &Ctx, locate: &mut Vec<Class>) -> Option<()> {
    let mut s = tail;
    while !s.is_empty() {
        if let Some(rest) = s.strip_prefix(MEMBER) {
            
            if rest.starts_with('[') {
                s = rest;
                continue;
            }
            let end = rest.find(|c| c == MEMBER || c == '[').unwrap_or(rest.len());
            let segment = &rest[..end];
            if segment.is_empty() {
                return None;
            }
            if let Some(key) = segment.strip_prefix('*') {
                if let Operand::Slot(slot) = parse_operand(key, ctx)? {
                    locate.extend(slot.classes);
                    locate.extend(slot.locate);
                }
            }
            s = &rest[end..];
        } else if s.starts_with('[') {
            let close = s.find(']')?;
            for part in s[1..close].split(',') {
                if let Operand::Slot(slot) = parse_operand(part, ctx)? {
                    locate.extend(slot.classes);
                    locate.extend(slot.locate);
                }
            }
            s = &s[close + 1..];
        } else {
            return None;
        }
    }
    Some(())
}

fn param_ref(base: &str, ctx: &Ctx) -> Option<Option<ParamRef>> {
    if let Some(inner) = base.strip_prefix('[') {
        let inner = inner.strip_suffix(']')?;
        let (row, col) = inner.split_once(',')?;
        if row.trim() != "0" {
            return None;
        }
        let col: i64 = col.trim().parse().ok()?;
        let index = col.unsigned_abs() as usize;
        return match col {
            c if c < 0 && index <= ctx.reads.len() => Some(Some(ParamRef::Read(index - 1))),
            c if c > 0 && index <= ctx.writes.len() => Some(Some(ParamRef::Write(index - 1))),
            _ => None,
        };
    }
    if let Some(i) = ctx.reads.iter().position(|p| p.name == base) {
        return Some(Some(ParamRef::Read(i)));
    }
    if let Some(i) = ctx.writes.iter().position(|p| p.name == base) {
        return Some(Some(ParamRef::Write(i)));
    }
    Some(None)
}

fn heap_slot(
    classes: Vec<Class>,
    locate: Vec<Class>,
    params: (Option<usize>, Option<usize>),
) -> Slot {
    Slot {
        classes,
        locate,
        bare: None,
        wparam: params.0,
        rparam: params.1,
        local: None,
    }
}

fn parse_operand(text: &str, ctx: &Ctx) -> Option<Operand> {
    let t = text.trim();
    if t.is_empty() {
        return None;
    }
    if let Some(rest) = t.strip_prefix('"') {
        
        return match rest.find('"').map(|end| &rest[end + 1..]) {
            Some(tail) if tail.starts_with(MEMBER) => {
                let mut locate = Vec::new();
                parse_tail(tail, ctx, &mut locate)?;
                Some(Operand::Slot(heap_slot(
                    vec![Class::Heap],
                    locate,
                    (None, None),
                )))
            }
            _ => Some(Operand::Ignore),
        };
    }
    if let Some(rest) = t.strip_prefix('\'') {
        
        let inner = rest.strip_suffix('\'').unwrap_or(rest);
        return if inner.starts_with('/') {
            parse_operand(inner, ctx)
        } else {
            Some(Operand::Ignore)
        };
    }
    if is_literal(t) {
        return Some(Operand::Ignore);
    }
    let (depth, base, tail) = split_base(t)?;
    if base == "_" && depth == 0 && tail.is_empty() {
        return Some(Operand::Ignore);
    }
    let mut locate = Vec::new();
    parse_tail(tail, ctx, &mut locate)?;
    let (own, value, params, local) = match param_ref(base, ctx)? {
        Some(ParamRef::Read(i)) => (Class::Params, ctx.reads[i].value, (None, Some(i)), None),
        Some(ParamRef::Write(k)) => (Class::Params, ctx.writes[k].value, (Some(k), None), None),
        None if base.starts_with('/') => (Class::Abs(base.to_string()), true, (None, None), None),
        None if is_ident(base) => {
            let value = ctx.local_types.get(base).is_some_and(|t| value_type(t));
            let local = Some(base.to_string());
            (Class::Local(base.to_string()), value, (None, None), local)
        }
        None => return None,
    };
    
    let direct = usize::from(own == Class::Params);
    let slot = if !tail.is_empty() {
        
        locate.push(own);
        heap_slot(vec![Class::Heap], locate, params)
    } else if depth > direct {
        locate.push(own);
        heap_slot(vec![Class::Heap, Class::Bare], locate, params)
    } else {
        let mut classes = vec![own.clone()];
        if !value {
            classes.push(Class::Heap);
        }
        Slot {
            classes,
            locate,
            bare: (direct == 0).then_some(own),
            wparam: params.0,
            rparam: params.1,
            local,
        }
    };
    Some(Operand::Slot(slot))
}

#[derive(Default)]
struct Step {
    op: String,
    
    spec: Option<OpSpec>,
    reads: Vec<Class>,
    
    writes: Vec<(Class, bool)>,
    
    read_args: Vec<Option<(Vec<Class>, Option<usize>)>>,
    wparam_reads: Vec<usize>,
    
    rparam_writes: Vec<usize>,
    
    slot_reads: Vec<(Vec<Class>, Option<usize>)>,
    
    partial: bool,
    unknown: bool,
    control: bool,
    
    volatile: Option<&'static str>,
    
    slot_sizes: Vec<(Vec<Class>, Option<Class>, Option<usize>)>,
    
    single_store: bool,
}

fn sized(classes: &[Class]) -> Vec<Class> {
    classes
        .iter()
        .map(|c| Class::Size(Box::new(c.clone())))
        .collect()
}

fn element_matches(input: &StepInput, ctx: &Ctx) -> bool {
    let element = input
        .reads
        .first()
        .and_then(|a| ctx.local_types.get(a.trim()))
        .and_then(|t| {
            t.strip_prefix('[')?
                .split_once(']')
                .map(|(_, e)| e.to_string())
        });
    match (element, input.write_types.first()) {
        (Some(e), Some(w)) => fixed_width_type(&e) && e == w.trim(),
        _ => false,
    }
}

fn parse_step(input: &StepInput, ctx: &Ctx, addr_taken: &mut BTreeSet<String>) -> Step {
    let spec = match &input.callee {
        Callee::Op(s) => Some(*s),
        Callee::Unverified(_) | Callee::Store(_) | Callee::Unknown(_) => Some(unverified()),
        Callee::None | Callee::Unit(_) => None,
    };
    let write_mode = match spec.map(|s| s.write) {
        Some(WriteMode::Element) if element_matches(input, ctx) => WriteMode::Whole,
        Some(WriteMode::Element) => WriteMode::Partial,
        Some(mode) => mode,
        None => WriteMode::Whole,
    };
    let mut step = Step {
        single_store: matches!(input.callee, Callee::Op(_)),
        op: input
            .op
            .strip_prefix("/lib/")
            .unwrap_or(&input.op)
            .to_string(),
        spec,
        partial: spec.is_some() && write_mode == WriteMode::Partial,
        volatile: match &input.callee {
            Callee::Op(s) if s.volatile => Some("volatile"),
            Callee::Unverified(_) => Some("unverified"),
            _ => None,
        },
        ..Step::default()
    };
    let reads: &[String] = match input.op.as_str() {
        "" | "goto" | "return" => {
            step.control = true;
            &[]
        }
        "br" => {
            step.control = true;
            &input.reads[..input.reads.len().min(1)]
        }
        _ => &input.reads,
    };
    
    let escapes = matches!(
        input.callee,
        Callee::Unit(_) | Callee::Store(_) | Callee::Unverified(_) | Callee::Unknown(_)
    );
    let copy = matches!(input.op.as_str(), "=" | "copy");
    
    let address_of = matches!(input.op.as_str(), "pathspace/key·abs" | "pathlang·resolvekey");
    let patches = spec.map_or(0, |s| s.patches);
    for (i, read) in reads.iter().enumerate() {
        match parse_operand(read, ctx) {
            None => {
                step.unknown = true;
                step.read_args.push(None);
            }
            Some(Operand::Ignore) => step.read_args.push(None),
            Some(Operand::Slot(slot)) => {
                if let Some(name) = &slot.local {
                    let container_copy = copy && slot.classes.contains(&Class::Heap);
                    if escapes || container_copy || (address_of && i == 0) {
                        addr_taken.insert(name.clone());
                    }
                }
                if patches == u32::MAX || (i < 32 && (patches >> i) & 1 == 1) {
                    let fixed = slot.local.as_ref().and_then(|n| ctx.local_types.get(n));
                    if !fixed.is_some_and(|t| fixed_width_type(t)) {
                        step.reads.extend(sized(&slot.classes));
                    }
                    step.writes
                        .extend(slot.classes.iter().map(|c| (c.clone(), false)));
                    step.rparam_writes.extend(slot.rparam);
                }
                step.read_args
                    .push(Some((slot.classes.clone(), slot.rparam)));
                step.reads.extend(slot.classes);
                step.reads.extend(slot.locate);
                if let Some(k) = slot.wparam {
                    step.wparam_reads.push(k);
                }
            }
        }
    }
    let kills = matches!(input.callee, Callee::Op(_)) && write_mode == WriteMode::Whole;
    for (j, write) in input.writes.iter().enumerate() {
        match parse_operand(write, ctx) {
            Some(Operand::Slot(slot)) => {
                if let (true, Some(name)) = (escapes, &slot.local) {
                    addr_taken.insert(name.clone());
                }
                let ty = input
                    .write_types
                    .get(j)
                    .filter(|t| !t.is_empty())
                    .or_else(|| slot.local.as_ref().and_then(|n| ctx.local_types.get(n)));
                let kill = kills && ty.is_some_and(|t| killable_type(t));
                let size_classes = match ty {
                    Some(t) if fixed_width_type(t) => Vec::new(),
                    _ => sized(&slot.classes),
                };
                let bare_size = slot.bare.clone().map(|b| Class::Size(Box::new(b)));
                step.slot_sizes.push((size_classes, bare_size, slot.wparam));
                if let (Some(k), false) = (slot.wparam, slot.locate.is_empty()) {
                    step.wparam_reads.push(k);
                }
                step.reads.extend(slot.locate.iter().cloned());
                let mut synthetic = slot.classes.clone();
                synthetic.extend(slot.locate);
                step.slot_reads.push((synthetic, slot.wparam));
                
                let bare_copy = copy && slot.bare.is_some();
                for class in slot.classes {
                    if bare_copy && class == Class::Heap {
                        continue;
                    }
                    let whole = kill && slot.bare.as_ref() == Some(&class);
                    step.writes.push((class, whole));
                }
            }
            Some(Operand::Ignore) if write.trim() == "_" => {
                step.slot_reads.push((Vec::new(), None));
                step.slot_sizes.push((Vec::new(), None, None));
            }
            _ => {
                step.unknown = true;
                step.slot_reads.push((Vec::new(), None));
                step.slot_sizes.push((Vec::new(), None, None));
            }
        }
    }
    step
}

#[derive(Default)]
struct Access {
    reads: Vec<Class>,
    writes: Vec<(Class, bool)>,
    
    exempt: Vec<Class>,
    
    withheld: Vec<Class>,
}

impl Access {
    fn exempt(&self, class: &Class) -> bool {
        self.exempt.contains(class)
    }
}

fn size_conflict(accesses: &[Access], inner: &Class) -> bool {
    let size = Class::Size(Box::new(inner.clone()));
    accesses.iter().enumerate().any(|(i, a)| {
        a.reads.contains(&size)
            && accesses.iter().enumerate().any(|(j, b)| {
                (i != j || !a.exempt(&size)) && b.writes.iter().any(|(w, _)| *w == size)
            })
    })
}

#[derive(Clone, Default, PartialEq, Eq)]
struct Summary {
    reads_wparam: Vec<bool>,
    
    writes_rparam: Vec<bool>,
    
    sizes_wparam: Vec<bool>,
    abs_reads: BTreeSet<String>,
    abs_writes: BTreeSet<String>,
    heap_read: bool,
    heap_write: bool,
    
    size_reads: BTreeSet<Class>,
    size_writes: BTreeSet<Class>,
    size_conflicts: BTreeSet<Class>,
}

struct Parsed {
    steps: Vec<Step>,
    addr_taken: BTreeSet<String>,
}

fn synthetic_positions(
    callee: &Callee,
    step: &Step,
    units: &[FunctionInput],
    sums: &[Summary],
) -> Vec<usize> {
    (0..step.slot_reads.len())
        .filter(|&j| match callee {
            Callee::None => false,
            Callee::Op(_) => step.partial,
            Callee::Unverified(_) => true,
            Callee::Unit(g) => {
                sums[*g].reads_wparam.get(j).copied().unwrap_or(true)
                    || units[*g].writes.get(j).map(|p| p.inplace).unwrap_or(true)
            }
            Callee::Store(_) | Callee::Unknown(_) => true,
        })
        .collect()
}

fn aliases(classes: &[Class], heap: &[Class], bare: &[Class], params_abs: bool) -> Vec<Class> {
    let mut out = Vec::new();
    for class in classes {
        let (inner, size) = match class {
            Class::Size(inner) => (inner.as_ref(), true),
            other => (other, false),
        };
        let mut add: Vec<Class> = Vec::new();
        match inner {
            Class::Bare => {
                add.push(Class::Heap);
                add.extend(bare.iter().cloned());
                add.extend(heap.iter().cloned());
            }
            Class::Heap => add.extend(heap.iter().cloned()),
            Class::Params if params_abs => add.extend(heap.iter().cloned()),
            _ => {}
        }
        out.extend(
            add.into_iter()
                .map(|c| if size { Class::Size(Box::new(c)) } else { c }),
        );
    }
    out
}

fn assemble(
    fi: usize,
    parsed: &Parsed,
    units: &[FunctionInput],
    sums: &[Summary],
    params_abs: bool,
) -> (Vec<Access>, Summary) {
    let mut summary = Summary {
        reads_wparam: vec![false; units[fi].writes.len()],
        writes_rparam: vec![false; units[fi].reads.len()],
        sizes_wparam: vec![false; units[fi].writes.len()],
        ..Summary::default()
    };
    let mut accesses: Vec<Access> = Vec::with_capacity(parsed.steps.len());
    let path = [Class::Heap, Class::Bare];
    for (input, step) in units[fi].steps.iter().zip(&parsed.steps) {
        let mut access = Access {
            reads: step.reads.clone(),
            writes: step.writes.clone(),
            exempt: Vec::new(),
            withheld: Vec::new(),
        };
        for &k in &step.wparam_reads {
            summary.reads_wparam[k] = true;
        }
        for &k in &step.rparam_writes {
            summary.writes_rparam[k] = true;
        }
        for j in synthetic_positions(&input.callee, step, units, sums) {
            let (classes, wparam) = &step.slot_reads[j];
            access.reads.extend(classes.iter().cloned());
            if let Some(k) = wparam {
                summary.reads_wparam[*k] = true;
            }
        }
        let (heap_read, heap_write) = match &input.callee {
            Callee::Unit(g) => {
                let callee = &sums[*g];
                
                for (j, arg) in step.read_args.iter().enumerate() {
                    let written = callee.writes_rparam.get(j).copied().unwrap_or(true);
                    if let (true, Some((classes, rparam))) = (written, arg) {
                        access
                            .writes
                            .extend(classes.iter().map(|c| (c.clone(), false)));
                        access.reads.extend(sized(classes));
                        if !callee.size_conflicts.contains(&Class::Params) {
                            access.exempt.extend(sized(classes));
                        }
                        if let Some(k) = rparam {
                            summary.writes_rparam[*k] = true;
                        }
                    }
                }
                access
                    .reads
                    .extend(callee.abs_reads.iter().map(|k| Class::Abs(k.clone())));
                access.writes.extend(
                    callee
                        .abs_writes
                        .iter()
                        .map(|k| (Class::Abs(k.clone()), false)),
                );
                
                let widen = |c: &Class| match c {
                    Class::Heap => path.to_vec(),
                    Class::Params => Vec::new(),
                    other => vec![other.clone()],
                };
                for c in &callee.size_reads {
                    access.reads.extend(sized(&widen(c)));
                }
                for c in &callee.size_writes {
                    access
                        .writes
                        .extend(sized(&widen(c)).into_iter().map(|c| (c, false)));
                }
                for c in callee.size_reads.union(&callee.size_writes) {
                    if callee.size_conflicts.contains(c) {
                        access.withheld.extend(sized(&widen(c)));
                    } else {
                        access.exempt.extend(sized(&widen(c)));
                    }
                }
                (callee.heap_read, callee.heap_write)
            }
            _ => step
                .spec
                .map_or((false, false), |s| (s.heap_read, s.heap_write)),
        };
        if heap_read {
            access.reads.extend(path.iter().cloned());
        }
        if heap_write {
            access
                .writes
                .extend(path.iter().map(|c| (c.clone(), false)));
        }
        for (j, (classes, bare, wparam)) in step.slot_sizes.iter().enumerate() {
            
            let unbound_path = matches!(bare, Some(Class::Size(b)) if matches!(b.as_ref(), Class::Abs(_)))
                || (bare.is_none()
                    && wparam.is_none()
                    && !classes.contains(&Class::Size(Box::new(Class::Params))));
            let store = match &input.callee {
                Callee::Op(spec) if spec.store == Store::InPlace && unbound_path => Store::Both,
                Callee::Op(spec) => spec.store,
                Callee::Unit(g) if sums[*g].sizes_wparam.get(j).copied().unwrap_or(true) => {
                    Store::Both
                }
                
                Callee::Unit(_) => Store::Untouched,
                Callee::None => Store::Untouched,
                _ => Store::Both,
            };
            let unit_writes = matches!(input.callee, Callee::Unit(_));
            match &input.callee {
                Callee::Op(_) => access.exempt.extend(classes.iter().cloned()),
                Callee::Unit(g) if !sums[*g].size_conflicts.contains(&Class::Params) => {
                    access.exempt.extend(classes.iter().cloned())
                }
                Callee::Unit(_) => access.withheld.extend(classes.iter().cloned()),
                _ => {}
            }
            if matches!(store, Store::InPlace | Store::Both) {
                access.reads.extend(classes.iter().cloned());
                if let Some(k) = wparam {
                    summary.sizes_wparam[*k] = true;
                }
            }
            if matches!(store, Store::Create | Store::Both) || unit_writes {
                let kill = store == Store::Create && matches!(input.callee, Callee::Op(_));
                access.writes.extend(
                    classes
                        .iter()
                        .map(|c| (c.clone(), kill && bare.as_ref() == Some(c))),
                );
            }
        }
        accesses.push(access);
    }
    let mut heap: BTreeSet<Class> = BTreeSet::from([Class::Params]);
    for access in &accesses {
        let all = access
            .reads
            .iter()
            .chain(access.writes.iter().map(|(c, _)| c));
        heap.extend(all.filter(|c| matches!(c, Class::Abs(_))).cloned());
    }
    let heap: Vec<Class> = heap.into_iter().collect();
    let bare: Vec<Class> = parsed
        .addr_taken
        .iter()
        .map(|n| Class::Local(n.clone()))
        .collect();
    for access in &mut accesses {
        let extra = aliases(&access.reads, &heap, &bare, params_abs);
        access.reads.extend(extra);
        let extra = aliases(&access.exempt, &heap, &bare, params_abs);
        access.exempt.extend(extra);
        let mut blocked = aliases(&access.withheld, &heap, &bare, params_abs);
        blocked.extend(access.withheld.iter().cloned());
        access.exempt.retain(|c| !blocked.contains(c));
        let written: Vec<Class> = access.writes.iter().map(|(c, _)| c.clone()).collect();
        let extra = aliases(&written, &heap, &bare, params_abs);
        access.writes.extend(extra.into_iter().map(|c| (c, false)));
        for class in &access.reads {
            match class {
                Class::Abs(k) => {
                    summary.abs_reads.insert(k.clone());
                }
                Class::Heap => summary.heap_read = true,
                _ => {}
            }
        }
        for (class, _) in &access.writes {
            match class {
                Class::Abs(k) => {
                    summary.abs_writes.insert(k.clone());
                }
                Class::Heap => summary.heap_write = true,
                _ => {}
            }
        }
    }
    for (access, step) in accesses.iter_mut().zip(&parsed.steps) {
        if step.single_store {
            let sizes = access
                .reads
                .iter()
                .chain(access.writes.iter().map(|(c, _)| c));
            let sizes: Vec<Class> = sizes
                .filter(|c| matches!(c, Class::Size(_)))
                .cloned()
                .collect();
            access.exempt.extend(sizes);
        }
    }
    let persistent = |c: &Class| matches!(c, Class::Abs(_) | Class::Heap | Class::Params);
    for access in &accesses {
        for class in &access.reads {
            if let Class::Size(inner) = class {
                if persistent(inner) {
                    summary.size_reads.insert(inner.as_ref().clone());
                }
            }
        }
        for (class, _) in &access.writes {
            if let Class::Size(inner) = class {
                if persistent(inner) {
                    summary.size_writes.insert(inner.as_ref().clone());
                }
            }
        }
    }
    let both: Vec<Class> = summary
        .size_reads
        .intersection(&summary.size_writes)
        .cloned()
        .collect();
    for inner in both {
        if size_conflict(&accesses, &inner) {
            summary.size_conflicts.insert(inner);
        }
    }
    (accesses, summary)
}

fn params_alias_paths(units: &[FunctionInput], parsed: &[Parsed]) -> Vec<bool> {
    let mut alias = vec![false; units.len()];
    loop {
        let mut changed = false;
        for (fi, f) in units.iter().enumerate() {
            for (input, step) in f.steps.iter().zip(&parsed[fi].steps) {
                let Callee::Unit(g) = input.callee else {
                    continue;
                };
                let passes = step
                    .reads
                    .iter()
                    .chain(step.writes.iter().map(|(c, _)| c))
                    .any(|c| match c {
                        Class::Abs(_) | Class::Heap | Class::Bare => true,
                        Class::Params => alias[fi],
                        Class::Local(_) | Class::Size(_) => false,
                    });
                if passes && !alias[g] {
                    alias[g] = true;
                    changed = true;
                }
            }
        }
        if !changed {
            return alias;
        }
    }
}

fn cyclic(units: &[FunctionInput]) -> Vec<bool> {
    let n = units.len();
    let edges: Vec<Vec<usize>> = units
        .iter()
        .map(|f| {
            f.steps
                .iter()
                .filter_map(|s| match s.callee {
                    Callee::Unit(g) => Some(g),
                    _ => None,
                })
                .collect()
        })
        .collect();
    
    (0..n)
        .map(|f| {
            let mut seen = vec![false; n];
            let mut stack: Vec<usize> = edges[f].clone();
            while let Some(g) = stack.pop() {
                if g == f {
                    return true;
                }
                if !std::mem::replace(&mut seen[g], true) {
                    stack.extend(edges[g].iter().copied());
                }
            }
            false
        })
        .collect()
}

struct Facts {
    unknown: Option<usize>,
    volatile: Option<usize>,
    abs_rw: Option<String>,
    heap_rw: bool,
    params_rw: bool,
}

fn facts(parsed: &Parsed, accesses: &[Access]) -> Facts {
    let read = |c: &Class| accesses.iter().any(|a| a.reads.contains(c));
    let written = |c: &Class| {
        accesses
            .iter()
            .any(|a| a.writes.iter().any(|(w, _)| w == c))
    };
    let abs_rw = accesses
        .iter()
        .flat_map(|a| a.writes.iter().map(|(c, _)| c))
        .filter_map(|c| match c {
            Class::Abs(k) if read(c) => Some(k.clone()),
            _ => None,
        })
        .min();
    let size_rw = |c: &Class| size_conflict(accesses, c);
    let abs_rw = abs_rw.or_else(|| {
        accesses
            .iter()
            .flat_map(|a| a.writes.iter().map(|(c, _)| c))
            .filter_map(|c| match c {
                Class::Size(inner) => match inner.as_ref() {
                    Class::Abs(k) if size_rw(inner) => Some(k.clone()),
                    _ => None,
                },
                _ => None,
            })
            .min()
    });
    Facts {
        unknown: parsed.steps.iter().position(|s| s.unknown),
        volatile: parsed.steps.iter().position(|s| s.volatile.is_some()),
        abs_rw,
        heap_rw: (read(&Class::Heap) && written(&Class::Heap)) || size_rw(&Class::Heap),
        params_rw: (read(&Class::Params) && written(&Class::Params)) || size_rw(&Class::Params),
    }
}

fn step_replayable(callee: &Callee, replayable: &[bool]) -> bool {
    match callee {
        Callee::None | Callee::Unverified(_) => true,
        Callee::Op(spec) => !spec.opaque,
        Callee::Unit(g) => replayable[*g],
        Callee::Store(_) | Callee::Unknown(_) => false,
    }
}

pub fn analyze_unit(units: &[FunctionInput]) -> Vec<Resumability> {
    let parsed: Vec<Parsed> = units
        .iter()
        .map(|f| {
            let ctx = Ctx {
                reads: &f.reads,
                writes: &f.writes,
                local_types: &f.local_types,
            };
            let mut addr_taken = BTreeSet::new();
            let steps = f
                .steps
                .iter()
                .map(|s| parse_step(s, &ctx, &mut addr_taken))
                .collect();
            Parsed { steps, addr_taken }
        })
        .collect();
    let params_abs = params_alias_paths(units, &parsed);
    
    let mut sums: Vec<Summary> = units
        .iter()
        .map(|f| Summary {
            reads_wparam: vec![false; f.writes.len()],
            writes_rparam: vec![false; f.reads.len()],
            sizes_wparam: vec![false; f.writes.len()],
            ..Summary::default()
        })
        .collect();
    let mut accesses: Vec<Vec<Access>> = Vec::new();
    loop {
        let mut changed = false;
        accesses.clear();
        for fi in 0..units.len() {
            let (acc, summary) = assemble(fi, &parsed[fi], units, &sums, params_abs[fi]);
            if summary != sums[fi] {
                sums[fi] = summary;
                changed = true;
            }
            accesses.push(acc);
        }
        if !changed {
            break;
        }
    }
    let facts: Vec<Facts> = (0..units.len())
        .map(|f| facts(&parsed[f], &accesses[f]))
        .collect();
    let cyclic = cyclic(units);
    
    let mut replayable: Vec<Option<bool>> = vec![None; units.len()];
    fn resolve(
        f: usize,
        units: &[FunctionInput],
        facts: &[Facts],
        cyclic: &[bool],
        memo: &mut Vec<Option<bool>>,
    ) -> bool {
        if let Some(v) = memo[f] {
            return v;
        }
        let own = !cyclic[f]
            && facts[f].unknown.is_none()
            && facts[f].volatile.is_none()
            && facts[f].abs_rw.is_none()
            && !facts[f].heap_rw;
        memo[f] = Some(own);
        let mut value = own;
        if own {
            for step in &units[f].steps {
                let ok = match &step.callee {
                    Callee::Unit(g) => resolve(*g, units, facts, cyclic, memo),
                    other => step_replayable(other, &[]),
                };
                if !ok {
                    value = false;
                    break;
                }
            }
        }
        memo[f] = Some(value);
        value
    }
    for f in 0..units.len() {
        resolve(f, units, &facts, &cyclic, &mut replayable);
    }
    let replayable: Vec<bool> = replayable.into_iter().map(|v| v.unwrap_or(false)).collect();
    (0..units.len())
        .map(|f| verdict(f, units, &parsed[f], &accesses[f], &facts[f], &replayable))
        .collect()
}

fn block_starts(f: &FunctionInput, steps: &[Step]) -> Vec<usize> {
    let n = steps.len();
    let mut start = vec![false; n + 1];
    if n > 0 {
        start[0] = true;
    }
    for &l in &f.label_starts {
        if l <= n {
            start[l] = true;
        }
    }
    for (i, s) in steps.iter().enumerate() {
        if s.control {
            start[i + 1] = true;
        }
    }
    let mut out = vec![0; n];
    let mut current = 0;
    for i in 0..n {
        if start[i] {
            current = i;
        }
        out[i] = current;
    }
    out
}

fn region_safe(
    a: usize,
    c: usize,
    f: &FunctionInput,
    parsed: &Parsed,
    accesses: &[Access],
    replayable: &[bool],
    written_globals: &BTreeSet<Class>,
) -> bool {
    let mut killed: BTreeSet<&Class> = BTreeSet::new();
    let mut exposed: BTreeSet<&Class> = BTreeSet::new();
    for i in a..=c {
        if parsed.steps[i].unknown
            || (a < c && parsed.steps[i].volatile.is_some())
            || !step_replayable(&f.steps[i].callee, replayable)
        {
            return false;
        }
        
        let late = |r: &Class| accesses[i].exempt(r);
        for r in accesses[i].reads.iter().filter(|r| !late(r)) {
            if a < c && written_globals.contains(r) {
                return false;
            }
            if !killed.contains(r) {
                exposed.insert(r);
            }
        }
        for (w, whole) in &accesses[i].writes {
            if exposed.contains(w) {
                return false;
            }
            if *whole {
                killed.insert(w);
            }
        }
        for r in accesses[i].reads.iter().filter(|r| late(r)) {
            if !killed.contains(r) {
                exposed.insert(r);
            }
        }
    }
    true
}

fn verdict(
    fi: usize,
    units: &[FunctionInput],
    parsed: &Parsed,
    accesses: &[Access],
    facts: &Facts,
    replayable: &[bool],
) -> Resumability {
    let f = &units[fi];
    let n = parsed.steps.len();
    let starts = block_starts(f, &parsed.steps);
    let written_globals: BTreeSet<Class> = accesses
        .iter()
        .flat_map(|a| a.writes.iter().map(|(c, _)| c.clone()))
        .filter(|c| matches!(c, Class::Abs(_) | Class::Heap))
        .collect();
    
    let entry_block = if facts.unknown.is_some() {
        Some("unknown".to_string())
    } else if let Some((_, step)) = f
        .steps
        .iter()
        .zip(&parsed.steps)
        .find(|(s, _)| !step_replayable(&s.callee, replayable))
    {
        Some(format!("callee:{}", step.op))
    } else if let Some(i) = facts.volatile {
        let kind = parsed.steps[i].volatile.unwrap_or("volatile");
        Some(format!("{kind}:{}", parsed.steps[i].op))
    } else if let Some(key) = &facts.abs_rw {
        Some(key.clone())
    } else if facts.heap_rw {
        Some("heap".to_string())
    } else if !replayable[fi] {
        Some(format!("callee:{}", f.name))
    } else {
        None
    };
    let mut points: Vec<(usize, Replay)> = Vec::new();
    for c in 0..n {
        let anchor = (starts[c]..=c)
            .rev()
            .find(|&a| region_safe(a, c, f, parsed, accesses, replayable, &written_globals));
        match anchor {
            Some(a) if a == c => {}
            Some(a) => points.push((c, Replay::From(a))),
            None => match &entry_block {
                None if facts.params_rw => points.push((c, Replay::Caller)),
                None => points.push((c, Replay::Entry)),
                Some(reason) => {
                    return Resumability::NotResumable {
                        step: c,
                        reason: reason.clone(),
                    }
                }
            },
        }
    }
    if points.is_empty() {
        Resumability::AlwaysResumable
    } else {
        Resumability::ConditionallyResumable { points }
    }
}

pub fn analyze(f: FunctionInput) -> Resumability {
    analyze_unit(std::slice::from_ref(&f)).remove(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(op: &str, reads: &[&str], writes: &[&str]) -> StepInput {
        let callee = match op {
            "" | "goto" | "br" | "return" => Callee::None,
            other => op_spec(other).map_or(Callee::Unverified(other.to_string()), Callee::Op),
        };
        StepInput {
            op: op.to_string(),
            reads: reads.iter().map(|x| x.to_string()).collect(),
            writes: writes.iter().map(|x| x.to_string()).collect(),
            write_types: Vec::new(),
            callee,
        }
    }

    fn call(op: &str, reads: &[&str], writes: &[&str], callee: Callee) -> StepInput {
        StepInput {
            callee,
            ..s(op, reads, writes)
        }
    }

    fn p(name: &str, inplace: bool) -> ParamInfo {
        ParamInfo {
            name: name.to_string(),
            inplace,
            value: true,
        }
    }

    fn typed(mut f: FunctionInput, types: &[(&str, &str)]) -> FunctionInput {
        for (name, ty) in types {
            f.local_types.insert(name.to_string(), ty.to_string());
        }
        f
    }

    fn func(
        name: &str,
        reads: Vec<ParamInfo>,
        writes: Vec<ParamInfo>,
        steps: Vec<StepInput>,
        labels: &[usize],
    ) -> FunctionInput {
        let mut local_types = HashMap::new();
        for w in steps.iter().flat_map(|s| &s.writes) {
            if is_ident(w) && w != "_" {
                local_types.insert(w.clone(), "int64".to_string());
            }
        }
        FunctionInput {
            name: name.to_string(),
            reads,
            writes,
            steps,
            label_starts: labels.to_vec(),
            local_types,
        }
    }

    #[test]
    fn exempt_alias_cannot_bypass_callee_conflict() {
        let mixed = |extra_heap: bool| {
            let mut steps = vec![
                s("=", &["\"ab"], &["*[0,1]"]),
                s("string·concat", &["\"x", "\"yz"], &["*[0,1]"]),
            ];
            if extra_heap {
                steps.push(s("=", &["\"q"], &["t"]));
                steps.push(s("pathspace/key·abs", &["t"], &["_p"]));
                steps.push(s("=", &["\"r"], &["*_p"]));
            }
            steps.push(s("return", &[], &[]));
            typed(
                func("mixed", vec![], vec![p("r", false)], steps, &[]),
                &[("t", "[]char/utf8"), ("_p", "*[]char/utf8")],
            )
        };
        let user = || {
            typed(
                func(
                    "user",
                    vec![],
                    vec![],
                    vec![
                        s("=", &["\"zz"], &["v"]),
                        call("g", &[], &["v"], Callee::Unit(0)),
                        s("return", &[], &[]),
                    ],
                    &[],
                ),
                &[("v", "[]char/utf8")],
            )
        };
        let plain = analyze_unit(&[mixed(false), user()]);
        let with_heap = analyze_unit(&[mixed(true), user()]);
        assert_eq!(plain[1].to_string(), "CONDITIONALLY_RESUMABLE:1<-entry");
        assert_eq!(with_heap[1].to_string(), plain[1].to_string());
    }

    fn points(r: &Resumability) -> Vec<(usize, Replay)> {
        match r {
            Resumability::ConditionallyResumable { points } => points.clone(),
            other => panic!("expected CONDITIONALLY_RESUMABLE, got {other}"),
        }
    }

    fn add_to() -> FunctionInput {
        func(
            "add_to",
            vec![p("d", false)],
            vec![p("acc", true)],
            vec![
                s("=", &["*acc"], &["_1"]),
                s("=", &["*d"], &["_2"]),
                s("add", &["_1", "_2"], &["*acc"]),
                s("return", &[], &[]),
            ],
            &[],
        )
    }

    #[test]
    fn self_increment_anchors_at_initial_write() {
        let f = func(
            "inc",
            vec![],
            vec![],
            vec![
                s("=", &["1"], &["x"]),
                s("add", &["x", "1"], &["x"]),
                s("=", &["x"], &["/resume/y"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let r = analyze(f);
        assert_eq!(points(&r), vec![(1, Replay::From(0))]);
        assert_eq!(r.to_string(), "CONDITIONALLY_RESUMABLE:1<-0");
    }

    #[test]
    fn loop_increment_needs_entry() {
        let f = func(
            "loop",
            vec![],
            vec![],
            vec![
                s("=", &["0"], &["i"]),
                s("goto", &["_while_2"], &[]),
                s("lt", &["i", "10"], &["_1"]),
                s("br", &["_1", "_do_3", "_exit_4"], &[]),
                s("add", &["i", "1"], &["i"]),
                s("goto", &["_while_2"], &[]),
                s("return", &[], &[]),
            ],
            &[2, 4, 6],
        );
        assert_eq!(points(&analyze(f)), vec![(4, Replay::Entry)]);
    }

    #[test]
    fn window_has_no_replay_points() {
        let f = func(
            "window",
            vec![p("b0", false)],
            vec![p("c", true), p("x", true)],
            vec![
                s("=", &["*b0"], &["_1"]),
                s("add", &["_1", "0"], &["b"]),
                s("=", &["*c"], &["_2"]),
                s("add", &["_2", "1"], &["*c"]),
                s("=", &["*x"], &["_3"]),
                s("add", &["_3", "b"], &["*x"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(analyze(f), Resumability::AlwaysResumable);
    }

    #[test]
    fn param_rmw_needs_caller_and_user_rwir_is_unverified() {
        assert_eq!(analyze(add_to()), Resumability::AlwaysResumable);
        let set = func(
            "set0",
            vec![],
            vec![p("acc", false)],
            vec![
                s("compactarray·set", &["*acc", "0", "9"], &["*acc"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(points(&analyze(set)), vec![(0, Replay::Caller)]);
        let wrap = func(
            "wrap_acc",
            vec![p("d", false)],
            vec![p("acc", true)],
            vec![
                s("=", &["*d"], &["_1"]),
                s("native_acc", &["_1"], &["*acc"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(
            analyze(wrap).to_string(),
            "NOT_RESUMABLE:1:unverified:native_acc"
        );
    }

    #[test]
    fn caller_anchors_on_callee_reading_write_param() {
        let local = func(
            "local_caller",
            vec![],
            vec![],
            vec![
                s("=", &["10"], &["v"]),
                call("add_to", &["5"], &["v"], Callee::Unit(1)),
                s("return", &[], &[]),
            ],
            &[],
        );
        let global = func(
            "global_caller",
            vec![],
            vec![],
            vec![
                s("=", &["10"], &["/tmp/g"]),
                call("add_to", &["5"], &["/tmp/g"], Callee::Unit(1)),
                s("return", &[], &[]),
            ],
            &[],
        );
        let r = analyze_unit(&[local, add_to(), global]);
        assert_eq!(points(&r[0]), vec![(1, Replay::From(0))]);
        assert_eq!(r[2].to_string(), "NOT_RESUMABLE:1:/tmp/g");
    }

    #[test]
    fn unmarked_write_param_read_counts_as_callee_read() {
        let bump = func(
            "bump",
            vec![],
            vec![p("acc", false)],
            vec![
                s("=", &["*acc"], &["_1"]),
                s("add", &["_1", "1"], &["*acc"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let user = func(
            "user",
            vec![],
            vec![],
            vec![
                s("=", &["3"], &["v"]),
                call("bump", &[], &["v"], Callee::Unit(0)),
                s("=", &["v"], &["/tmp/o"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let r = analyze_unit(&[bump, user]);
        assert_eq!(r[0], Resumability::AlwaysResumable);
        assert_eq!(points(&r[1]), vec![(1, Replay::From(0))]);
    }

    #[test]
    fn plain_dataflow_is_always() {
        let f = func(
            "plain",
            vec![p("a", false)],
            vec![p("r", false)],
            vec![
                s("add", &["a", "1"], &["b"]),
                s("mul", &["b", "2"], &["*r"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(analyze(f), Resumability::AlwaysResumable);
    }

    #[test]
    fn unresolved_and_store_callees_are_not_resumable() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("=", &["0"], &["v"]),
                call("ext·g", &["1"], &["v"], Callee::Unknown("ext·g".into())),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(analyze(f).to_string(), "NOT_RESUMABLE:1:callee:ext·g");
        let g = func(
            "g",
            vec![],
            vec![],
            vec![
                call(
                    "mp·add_to",
                    &["1"],
                    &["_"],
                    Callee::Store("mp·add_to".into()),
                ),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(analyze(g).to_string(), "NOT_RESUMABLE:0:callee:mp·add_to");
        let h = func(
            "h",
            vec![],
            vec![],
            vec![s("vthread·create", &["\"x"], &["t"]), s("return", &[], &[])],
            &[],
        );
        assert_eq!(
            analyze(h).to_string(),
            "NOT_RESUMABLE:0:callee:vthread·create"
        );
    }

    #[test]
    fn unparsable_operand_is_unknown() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![s("add", &["", "1"], &["x"]), s("return", &[], &[])],
            &[],
        );
        assert_eq!(analyze(f).to_string(), "NOT_RESUMABLE:0:unknown");
    }

    #[test]
    fn read_then_overwrite_picks_latest_safe_anchor() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("=", &["1"], &["a"]),
                s("goto", &["L"], &[]),
                s("=", &["a"], &["t"]),
                s("add", &["t", "1"], &["a"]),
                s("add", &["a", "t"], &["a"]),
                s("=", &["a"], &["/tmp/o"]),
                s("return", &[], &[]),
            ],
            &[2],
        );
        assert_eq!(points(&analyze(f)), vec![(4, Replay::From(3))]);
    }

    #[test]
    fn join_block_does_not_anchor_in_a_branch() {
        let f = func(
            "j",
            vec![p("c", false)],
            vec![],
            vec![
                s("=", &["0"], &["x"]),
                s("gt", &["c", "0"], &["_1"]),
                s("br", &["_1", "_then", "_else"], &[]),
                s("=", &["5"], &["x"]),
                s("goto", &["_merge"], &[]),
                s("=", &["7"], &["x"]),
                s("goto", &["_merge"], &[]),
                s("add", &["x", "1"], &["x"]),
                s("=", &["x"], &["/tmp/out"]),
                s("return", &[], &[]),
            ],
            &[3, 5, 7],
        );
        assert_eq!(points(&analyze(f)), vec![(7, Replay::Entry)]);
    }

    #[test]
    fn partial_write_is_not_a_kill() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("map·new", &[], &["m"]),
                s("goto", &["L"], &[]),
                s("=", &["1"], &["m·f"]),
                s("=", &["m·g"], &["y"]),
                s("compactarray·set", &["m", "0", "y"], &["m"]),
                s("return", &[], &[]),
            ],
            &[2],
        );
        let f = typed(f, &[("m", "'[]char/utf8'int64")]);
        assert_eq!(analyze(f).to_string(), "NOT_RESUMABLE:4:heap");
    }

    #[test]
    fn partial_write_anchors_after_last_overwrite() {
        let f = func(
            "g1",
            vec![],
            vec![],
            vec![
                s("=", &["\"abc"], &["s"]),
                s("=", &["s"], &["t"]),
                s("=", &["\"xyz"], &["s"]),
                s("string·set", &["t", "0", "\"q"], &["s"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let f = typed(f, &[("s", "[]char/utf32"), ("t", "[]char/utf32")]);
        assert_eq!(points(&analyze(f)), vec![(3, Replay::Entry)]);
    }

    #[test]
    fn user_rwir_blocks_longer_ranges() {
        let f = func(
            "g1",
            vec![],
            vec![],
            vec![
                s("=", &["1"], &["s"]),
                s("=", &["s"], &["t"]),
                s("=", &["100"], &["s"]),
                s("native_acc", &["t"], &["s"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(
            analyze(f).to_string(),
            "NOT_RESUMABLE:3:unverified:native_acc"
        );
        let g = func(
            "g",
            vec![],
            vec![],
            vec![s("native_acc", &["5"], &[]), s("return", &[], &[])],
            &[],
        );
        
        assert_eq!(
            analyze(g).to_string(),
            "NOT_RESUMABLE:0:unverified:native_acc"
        );
    }

    #[test]
    fn unlisted_op_is_worst_case() {
        assert!(op_spec("compactarray·shape").is_none());
        assert!(op_spec("xv·set").is_none());
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("compactarray·new", &["1", "2"], &["a"]),
                s("compactarray·shape", &["a"], &["d"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let f = typed(f, &[("a", "[]int64"), ("d", "[1]int64")]);
        assert_eq!(
            analyze(f).to_string(),
            "NOT_RESUMABLE:1:unverified:compactarray·shape"
        );
    }

    #[test]
    fn listat_is_no_kill() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("=", &["\"init"], &["name"]),
                s("=", &["0"], &["x"]),
                s("pathspace·listat", &["/tmp/rc_dir", "99"], &["name"]),
                s("=", &["name"], &["u"]),
                s("=", &["\"zzzz"], &["name"]),
                s("add", &["x", "1"], &["x"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let f = typed(f, &[("name", "[]char/utf8"), ("u", "[]char/utf8")]);
        
        assert_eq!(
            points(&analyze(f)),
            vec![(2, Replay::From(0)), (5, Replay::Entry)]
        );
    }

    #[test]
    fn array_set_patches_its_read_operand() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("compactarray·new", &["1", "2", "3"], &["a"]),
                s("compactarray·new", &["0", "0", "0"], &["b"]),
                s("=", &["0"], &["x"]),
                s("compactarray·at", &["a", "0"], &["t"]),
                s("compactarray·set", &["a", "0", "9"], &["b"]),
                s("add", &["x", "t"], &["x"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let f = typed(f, &[("a", "[]int64"), ("b", "[]int64")]);
        let mut f = f;
        f.steps[3].write_types = vec!["int64".into()];
        
        assert_eq!(
            points(&analyze(f)),
            vec![(4, Replay::Entry), (5, Replay::Entry)]
        );
    }

    #[test]
    fn callee_patch_of_read_param_reaches_the_caller() {
        let poke = func(
            "poke",
            vec![p("a", false)],
            vec![p("r", false)],
            vec![
                s("compactarray·set", &["*[0,-1]", "0", "9"], &["*[0,1]"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let user = func(
            "test",
            vec![],
            vec![],
            vec![
                s("compactarray·new", &["1", "2", "3"], &["a"]),
                s("compactarray·new", &["0", "0", "0"], &["b"]),
                s("=", &["0"], &["x"]),
                s("compactarray·at", &["a", "0"], &["t"]),
                call("poke", &["a"], &["b"], Callee::Unit(0)),
                s("add", &["x", "t"], &["x"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let mut user = typed(user, &[("a", "[]int64"), ("b", "[]int64")]);
        user.steps[3].write_types = vec!["int64".into()];
        let r = analyze_unit(&[poke, user]);
        assert_eq!(r[0].to_string(), "CONDITIONALLY_RESUMABLE:0<-caller");
        assert_eq!(
            r[1].to_string(),
            "CONDITIONALLY_RESUMABLE:4<-entry,5<-entry"
        );
    }

    #[test]
    fn anchor_never_rereads_a_written_global() {
        let f = func(
            "g2b",
            vec![],
            vec![],
            vec![
                s("=", &["\"abc"], &["s"]),
                s("=", &["/tmp/g"], &["u"]),
                s("=", &["u"], &["v"]),
                s("=", &["\"xyz"], &["/tmp/g"]),
                s("string·set", &["v", "0", "\"q"], &["s"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let f = typed(
            f,
            &[
                ("s", "[]char/utf32"),
                ("u", "[]char/utf32"),
                ("v", "[]char/utf32"),
            ],
        );
        assert_eq!(analyze(f).to_string(), "NOT_RESUMABLE:4:/tmp/g");
    }

    #[test]
    fn heap_write_aliases_address_taken_local() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("pathspace/key·abs", &["x"], &["p"]),
                s("goto", &["L"], &[]),
                s("=", &["0"], &["z"]),
                s("=", &["x"], &["t"]),
                s("=", &["5"], &["*p"]),
                s("add", &["z", "t"], &["z"]),
                s("return", &[], &[]),
            ],
            &[2],
        );
        let f = typed(f, &[("x", "int64"), ("p", "*int64")]);
        assert_eq!(points(&analyze(f)), vec![(5, Replay::Entry)]);
    }

    #[test]
    fn recursion_is_not_replayable() {
        let f = func(
            "f",
            vec![p("n", false)],
            vec![p("acc", false)],
            vec![
                s("sub", &["n", "1"], &["_1"]),
                call("f", &["_1"], &["*acc"], Callee::Unit(0)),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(analyze(f).to_string(), "NOT_RESUMABLE:1:callee:f");
    }

    #[test]
    fn callee_global_effects_reach_the_caller() {
        let set = func(
            "set",
            vec![],
            vec![],
            vec![s("=", &["7"], &["/tmp/g"]), s("return", &[], &[])],
            &[],
        );
        let user = func(
            "user",
            vec![],
            vec![],
            vec![
                s("=", &["/tmp/g"], &["v"]),
                call("set", &[], &[], Callee::Unit(0)),
                s("add", &["v", "1"], &["v"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let r = analyze_unit(&[set, user]);
        assert_eq!(r[0], Resumability::AlwaysResumable);
        assert_eq!(r[1].to_string(), "NOT_RESUMABLE:2:/tmp/g");
    }

    #[test]
    fn by_value_param_shares_params_class() {
        let f = func(
            "f",
            vec![p("a", false)],
            vec![p("r", false)],
            vec![
                s("=", &["*[0,-1]"], &["*[0,1]"]),
                s("=", &["0"], &["x"]),
                s("=", &["*[0,-1]"], &["t"]),
                s("add", &["t", "1"], &["*[0,1]"]),
                s("add", &["x", "t"], &["x"]),
                s("=", &["x"], &["/tmp/o"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(
            analyze(f).to_string(),
            "CONDITIONALLY_RESUMABLE:0<-caller,4<-caller"
        );
    }

    #[test]
    fn volatile_step_blocks_longer_ranges() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("=", &["0"], &["x"]),
                s("random·intn", &["100"], &["k"]),
                s("add", &["x", "1"], &["x"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        assert_eq!(
            analyze(f).to_string(),
            "NOT_RESUMABLE:2:volatile:random·intn"
        );
        let g = func(
            "g",
            vec![],
            vec![],
            vec![s("random·intn", &["100"], &["k"]), s("return", &[], &[])],
            &[],
        );
        assert_eq!(analyze(g), Resumability::AlwaysResumable);
    }

    #[test]
    fn callee_argument_is_address_taken() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("=", &["1"], &["v"]),
                call("keep", &["v"], &[], Callee::Unit(1)),
                s("goto", &["L"], &[]),
                s("=", &["0"], &["z"]),
                s("=", &["v"], &["t"]),
                s("=", &["5"], &["*q"]),
                s("add", &["z", "t"], &["z"]),
                s("return", &[], &[]),
            ],
            &[3],
        );
        let f = typed(f, &[("q", "*int64")]);
        let keep = func(
            "keep",
            vec![p("x", false)],
            vec![],
            vec![s("return", &[], &[])],
            &[],
        );
        let r = analyze_unit(&[f, keep]);
        assert_eq!(points(&r[0]), vec![(6, Replay::Entry)]);
    }

    #[test]
    fn op_table_matches_catalog_and_corpus() {
        let rows: BTreeSet<String> = CATALOG
            .lines()
            .filter_map(|l| l.trim().strip_prefix("RUNTIMEC_RWIR_OPERATION("))
            .filter_map(|r| r.split('"').nth(1).map(str::to_string))
            .collect();
        for (name, _) in OP_TABLE {
            assert!(rows.contains(*name), "{name} is not a catalog row");
        }
        for name in [
            "!",
            "!=",
            "&&",
            "+",
            "<",
            "<<",
            "=",
            "==",
            "abs",
            "add",
            "and",
            "bitand",
            "bitor",
            "bitxor",
            "char/ascii",
            "char/utf32",
            "char/utf8",
            "compactarray·at",
            "compactarray·bodylen",
            "compactarray·copy",
            "compactarray·dim",
            "compactarray·fill",
            "compactarray·langtype",
            "compactarray·new",
            "compactarray·newreinterpret",
            "compactarray·newreshape",
            "compactarray·newslice",
            "compactarray·numel",
            "compactarray·parselangtype",
            "compactarray·set",
            "div",
            "eq",
            "exp",
            "float32",
            "float64",
            "ge",
            "gt",
            "int16",
            "int32",
            "int64",
            "int8",
            "pathspace/key·abs",
            "pathlang·resolvekey",
            "pathspace·cp",
            "pathspace·cpdir",
            "pathspace·del",
            "pathspace·deltree",
            "pathspace·listat",
            "pathspace·listlen",
            "pathspace·mkindex",
            "le",
            "log",
            "lt",
            "map·new",
            "max",
            "min",
            "mod",
            "mul",
            "neg",
            "neq",
            "not",
            "pow",
            "random·int63",
            "random·intn",
            "random·uint64",
            "scatterarray·append",
            "scatterarray·len",
            "scatterarray·newcompact",
            "scatterarray·newscatter",
            "scatterarray·slice",
            "shl",
            "shr",
            "sqrt",
            "string·char",
            "string·cmp",
            "string·concat",
            "string·find",
            "string·formatint",
            "string·formatuint",
            "string·len",
            "string·ord",
            "string·parsefloat",
            "string·parseint",
            "string·parseuint",
            "string·set",
            "string·slice",
            "struct·new",
            "sub",
            "time/duration·after",
            "time/duration·as_hours",
            "time/duration·as_millis",
            "time/duration·as_nanos",
            "time/duration·as_seconds",
            "time/duration·before",
            "time/duration·hours",
            "time/duration·millis",
            "time/duration·nanos",
            "time/duration·seconds",
            "time/duration·sub",
            "time·add",
            "time·after",
            "time·before",
            "time·now",
            "time·sub",
            "uint16",
            "uint32",
            "uint64",
            "uint8",
            "vthread·call",
            "vthread·create",
            "vthread·run",
            "vthread·sleep",
            "||",
        ] {
            assert!(op_spec(name).is_some(), "{name} must be verified");
        }
        assert_eq!(op_spec("+").map(|s| s.write), Some(WriteMode::Whole));
        assert_eq!(op_spec("compactarray·set").map(|s| s.patches), Some(1));
    }

    #[test]
    fn size_rule_blocks_ranges_across_a_rebind() {
        let f = func(
            "f",
            vec![],
            vec![],
            vec![
                s("=", &["\"ab"], &["s"]),
                s("=", &["0"], &["x"]),
                s("string·slice", &["t", "0", "1"], &["s"]),
                s("add", &["x", "1"], &["x"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let f = typed(f, &[("s", "[]char/utf8"), ("t", "[]char/utf8")]);
        assert_eq!(points(&analyze(f)), vec![(3, Replay::From(1))]);
    }

    #[test]
    fn callee_size_conflict_reaches_the_call_step() {
        let mixed = func(
            "mixed",
            vec![],
            vec![p("r", false)],
            vec![
                s("=", &["\"ab"], &["*[0,1]"]),
                s("string·concat", &["\"x", "\"yz"], &["*[0,1]"]),
                s("return", &[], &[]),
            ],
            &[],
        );
        let single = func(
            "single",
            vec![],
            vec![p("r", false)],
            vec![s("=", &["\"ab"], &["*[0,1]"]), s("return", &[], &[])],
            &[],
        );
        let user = |callee: usize| {
            let f = func(
                "user",
                vec![],
                vec![],
                vec![
                    s("=", &["\"zz"], &["v"]),
                    call("g", &[], &["v"], Callee::Unit(callee)),
                    s("return", &[], &[]),
                ],
                &[],
            );
            typed(f, &[("v", "[]char/utf8")])
        };
        let r = analyze_unit(&[mixed, single, user(0), user(1)]);
        
        assert_eq!(r[0], Resumability::AlwaysResumable);
        assert_eq!(r[1], Resumability::AlwaysResumable);
        assert_eq!(r[2].to_string(), "CONDITIONALLY_RESUMABLE:1<-entry");
        assert_eq!(r[3], Resumability::AlwaysResumable);
    }

    #[test]
    fn type_classes() {
        for t in [
            "int64",
            "bool",
            "char/utf32",
            "[]char/utf32",
            "time/duration",
        ] {
            assert!(killable_type(t) && value_type(t), "{t}");
        }
        for t in [
            "char/utf8",
            "[]int64",
            "[3,2]float64",
            "*int64",
            "*/lib/Node",
        ] {
            assert!(!killable_type(t) && value_type(t), "{t}");
        }
        for t in ["'[]char/utf8'int64", "/lib/Node", "Node", "", "int64|bool"] {
            assert!(!killable_type(t) && !value_type(t), "{t}");
        }
    }

    #[test]
    fn operand_forms_resolve_to_classes() {
        let reads = [p("d", false), p("n", false)];
        let writes = [p("acc", true)];
        let types: HashMap<String, String> = [
            ("x", "int64"),
            ("k", "int64"),
            ("i", "int64"),
            ("a", "[]int64"),
            ("m", "'[]char/utf8'int64"),
        ]
        .iter()
        .map(|(k, v)| (k.to_string(), v.to_string()))
        .collect();
        let ctx = Ctx {
            reads: &reads,
            writes: &writes,
            local_types: &types,
        };
        let class = |t: &str| match parse_operand(t, &ctx) {
            Some(Operand::Slot(slot)) => Some((slot.classes, slot.locate, slot.bare.is_some())),
            Some(Operand::Ignore) => None,
            None => panic!("{t} must parse"),
        };
        let local = |n: &str| Class::Local(n.to_string());
        let abs = |k: &str| Class::Abs(k.to_string());
        use Class::{Bare, Heap, Params};
        assert_eq!(class("*[0,1]"), Some((vec![Params], vec![], false)));
        assert_eq!(class("*[0,-2]"), Some((vec![Params], vec![], false)));
        assert_eq!(class("(*acc)·x"), Some((vec![Heap], vec![Params], false)));
        assert_eq!(class("x"), Some((vec![local("x")], vec![], true)));
        assert_eq!(class("m"), Some((vec![local("m"), Heap], vec![], true)));
        assert_eq!(
            class("m·*k"),
            Some((vec![Heap], vec![local("k"), local("m")], false))
        );
        assert_eq!(
            class("a[i, 0]"),
            Some((vec![Heap], vec![local("i"), local("a")], false))
        );
        assert_eq!(
            class("*_p1"),
            Some((vec![Heap, Bare], vec![local("_p1")], false))
        );
        assert_eq!(class("/tmp/g"), Some((vec![abs("/tmp/g")], vec![], true)));
        assert_eq!(
            class("/tmp/g·k"),
            Some((vec![Heap], vec![abs("/tmp/g")], false))
        );
        assert_eq!(
            class("\"/a\"·*k"),
            Some((vec![Heap], vec![local("k")], false))
        );
        assert_eq!(class("'/tmp/x'"), Some((vec![abs("/tmp/x")], vec![], true)));
        assert_eq!(class("\"acc"), None);
        assert_eq!(class("'[]char/utf8'int64"), None);
        assert_eq!(class("-1"), None);
        assert!(parse_operand("(a·b)·c", &ctx).is_none());
        assert!(parse_operand("[1,2]", &ctx).is_none());
    }
}

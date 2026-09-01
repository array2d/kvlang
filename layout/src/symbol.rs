//! 所有符号的权威双向查找表（对齐 symbol/symbol.go）。禁止其他模块 hardcode 符号字符串。

// ── 显示用字符串常量 ─────────────────────────────────────────────────

pub const ARROW_LEFT: &str = " <- ";
pub const ARROW_RIGHT: &str = " -> ";
pub const ARROW_EQ: &str = " = ";

#[derive(Clone, Copy)]
pub struct Entry {
    pub word: &'static str,
    pub glyphs: &'static [&'static str],
    pub precedence: i32, // 0 = 非中缀
    pub arith: bool,
    pub cmp: bool,
    pub unary: bool,
}

impl Default for Entry {
    fn default() -> Self {
        Entry {
            word: "",
            glyphs: &[],
            precedence: 0,
            arith: false,
            cmp: false,
            unary: false,
        }
    }
}

// ── 符号表（word 唯一主键） ──────────────────────────────────────────

static ENTRIES: &[Entry] = &[
    // 分组符
    Entry {
        word: "lparen",
        glyphs: &["("],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "rparen",
        glyphs: &[")"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "lbrace",
        glyphs: &["{"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "rbrace",
        glyphs: &["}"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "lbrack",
        glyphs: &["["],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "rbrack",
        glyphs: &["]"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "comma",
        glyphs: &[","],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "semicolon",
        glyphs: &[";"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "dot",
        glyphs: &["·"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "colon",
        glyphs: &[":"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    // 箭头 / 赋值（= 兼作 copy opcode）
    Entry {
        word: "assign",
        glyphs: &["<-", "->", "="],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    // 算术
    Entry {
        word: "add",
        glyphs: &["+"],
        precedence: 50,
        arith: true,
        cmp: false,
        unary: true,
    },
    Entry {
        word: "sub",
        glyphs: &["-"],
        precedence: 50,
        arith: true,
        cmp: false,
        unary: true,
    },
    Entry {
        word: "pointer",
        glyphs: &["*"],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "mul",
        glyphs: &["×"],
        precedence: 60,
        arith: true,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "div",
        glyphs: &["÷"],
        precedence: 60,
        arith: true,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "mod",
        glyphs: &["%"],
        precedence: 60,
        arith: true,
        cmp: false,
        unary: false,
    },
    // 比较
    Entry {
        word: "eq",
        glyphs: &["=="],
        precedence: 30,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "neq",
        glyphs: &["!=", "≠"],
        precedence: 30,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "lt",
        glyphs: &["<"],
        precedence: 40,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "gt",
        glyphs: &[">"],
        precedence: 40,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "le",
        glyphs: &["<=", "≤"],
        precedence: 40,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "ge",
        glyphs: &[">=", "≥"],
        precedence: 40,
        arith: false,
        cmp: true,
        unary: false,
    },
    // 逻辑
    Entry {
        word: "and",
        glyphs: &["&&"],
        precedence: 20,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "or",
        glyphs: &["||"],
        precedence: 10,
        arith: false,
        cmp: true,
        unary: false,
    },
    Entry {
        word: "not",
        glyphs: &["!"],
        precedence: 0,
        arith: false,
        cmp: true,
        unary: true,
    },
    // 数学符号
    Entry {
        word: "sqrt",
        glyphs: &["√"],
        precedence: 0,
        arith: true,
        cmp: false,
        unary: true,
    },
    // 位运算
    Entry {
        word: "bitand",
        glyphs: &["&"],
        precedence: 80,
        arith: true,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "bitor",
        glyphs: &["|"],
        precedence: 100,
        arith: true,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "bitxor",
        glyphs: &["^"],
        precedence: 90,
        arith: true,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "shl",
        glyphs: &["<<"],
        precedence: 70,
        arith: true,
        cmp: false,
        unary: false,
    },
    Entry {
        word: "shr",
        glyphs: &[">>"],
        precedence: 70,
        arith: true,
        cmp: false,
        unary: false,
    },
];

// ── 查询 ─────────────────────────────────────────────────────────────

/// 按 glyph 查 Entry（未命中返回零值）。
pub fn lookup(glyph: &str) -> Entry {
    for e in ENTRIES {
        if e.glyphs.contains(&glyph) {
            return *e;
        }
    }
    Entry::default()
}

/// 按 word 查 Entry（未命中返回零值）。
pub fn by_word(word: &str) -> Entry {
    for e in ENTRIES {
        if e.word == word {
            return *e;
        }
    }
    Entry::default()
}

fn is_ascii(s: &str) -> bool {
    s.bytes().all(|b| b < 128)
}

pub fn scanner_two_char_ops() -> Vec<String> {
    ENTRIES
        .iter()
        .flat_map(|e| e.glyphs.iter())
        .filter(|g| g.len() == 2 && is_ascii(g))
        .map(|g| g.to_string())
        .collect()
}

pub fn scanner_one_char_ops() -> Vec<u8> {
    ENTRIES
        .iter()
        .flat_map(|e| e.glyphs.iter())
        .filter(|g| g.len() == 1 && is_ascii(g))
        .map(|g| g.as_bytes()[0])
        .collect()
}

pub fn unicode_ops() -> Vec<String> {
    ENTRIES
        .iter()
        .flat_map(|e| e.glyphs.iter())
        .filter(|g| !is_ascii(g))
        .map(|g| g.to_string())
        .collect()
}

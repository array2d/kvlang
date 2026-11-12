
pub const ARROW_RIGHT: &str = " -> ";
pub const ARROW_EQ: &str = " = ";

#[derive(Clone, Copy)]
pub struct Entry {
    pub word: &'static str,
    pub glyphs: &'static [&'static str],
    pub precedence: i32, 
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

static ENTRIES: &[Entry] = &[
    
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
    
    Entry {
        word: "assign",
        glyphs: &["->", "="],
        precedence: 0,
        arith: false,
        cmp: false,
        unary: false,
    },
    
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
        unary: true, 
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
    
    Entry {
        word: "sqrt",
        glyphs: &["√"],
        precedence: 0,
        arith: true,
        cmp: false,
        unary: true,
    },
    
    Entry {
        word: "bitand",
        glyphs: &["&"],
        precedence: 80,
        arith: true,
        cmp: false,
        unary: true,
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

pub fn lookup(glyph: &str) -> Entry {
    for e in ENTRIES {
        if e.glyphs.contains(&glyph) {
            return *e;
        }
    }
    Entry::default()
}

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

pub fn scanner_two_char_ops() -> &'static [String] {
    static T: std::sync::OnceLock<Vec<String>> = std::sync::OnceLock::new();
    T.get_or_init(|| {
        ENTRIES
            .iter()
            .flat_map(|e| e.glyphs.iter())
            .filter(|g| g.len() == 2 && is_ascii(g))
            .map(|g| g.to_string())
            .collect()
    })
}

pub fn scanner_one_char_ops() -> &'static [u8] {
    static T: std::sync::OnceLock<Vec<u8>> = std::sync::OnceLock::new();
    T.get_or_init(|| {
        ENTRIES
            .iter()
            .flat_map(|e| e.glyphs.iter())
            .filter(|g| g.len() == 1 && is_ascii(g))
            .map(|g| g.as_bytes()[0])
            .collect()
    })
}

pub fn unicode_ops() -> &'static [String] {
    static T: std::sync::OnceLock<Vec<String>> = std::sync::OnceLock::new();
    T.get_or_init(|| {
        ENTRIES
            .iter()
            .flat_map(|e| e.glyphs.iter())
            .filter(|g| !is_ascii(g))
            .map(|g| g.to_string())
            .collect()
    })
}

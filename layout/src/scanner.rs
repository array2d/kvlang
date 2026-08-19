//! 词法分析（对齐 parser/scanner.go）。Scan(src) → Vec<Token>，末尾附 EOF 哨兵。

use std::fmt;

use super::symbol;

#[derive(Clone, Copy)]
pub struct Pos {
    pub line: i32, // 1-based
    pub col: i32,  // 1-based
}

#[derive(Clone)]
pub struct Diagnostic {
    pub pos: Pos,
    pub message: String,
    pub warn: bool,   // true = 警告
    pub info: bool,   // true = 提示（优先级高于 warn）
    pub source: String,
    pub src_file: String,
    pub src_name: String,
}

impl Diagnostic {
    pub fn string(&self) -> String {
        let kind = if self.info {
            "info"
        } else if self.warn {
            "warn"
        } else {
            "error"
        };
        let src = if self.src_name.is_empty() {
            self.src_file.as_str()
        } else {
            self.src_name.as_str()
        };
        if !src.is_empty() {
            format!("{kind}: {src}:{}:{}: {}", self.pos.line, self.pos.col, self.message)
        } else {
            format!("{kind}: {}:{}: {}", self.pos.line, self.pos.col, self.message)
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Ident,
    Literal,
    Arrow,
    LParen,
    RParen,
    Comma,
    LBrace,
    RBrace,
    LBrack,
    RBrack,
    Dot,
    Colon,
    Return,
    If,
    Else,
    For,
    While,
    Break,
    Continue,
    Newline,
    Comment,
    EOF,
}

impl Kind {
    pub fn to_str(self) -> &'static str {
        match self {
            Kind::Ident => "IDENT",
            Kind::Literal => "LITERAL",
            Kind::Arrow => "ARROW",
            Kind::LParen => "LPAREN",
            Kind::RParen => "RPAREN",
            Kind::Comma => "COMMA",
            Kind::LBrace => "LBRACE",
            Kind::RBrace => "RBRACE",
            Kind::LBrack => "LBRACK",
            Kind::RBrack => "RBRACK",
            Kind::Dot => "DOT",
            Kind::Colon => "COLON",
            Kind::Return => "RETURN",
            Kind::If => "IF",
            Kind::Else => "ELSE",
            Kind::For => "FOR",
            Kind::While => "WHILE",
            Kind::Break => "BREAK",
            Kind::Continue => "CONTINUE",
            Kind::Newline => "NEWLINE",
            Kind::Comment => "COMMENT",
            Kind::EOF => "EOF",
        }
    }
}

impl fmt::Display for Kind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.to_str())
    }
}

#[derive(Clone)]
pub struct Token {
    pub kind: Kind,
    pub value: String,
    pub pos: Pos,
    pub quote: u8, // 0=无, '"'=", '`'=`
}

impl Token {
    pub fn eof(pos: Pos) -> Token {
        Token { kind: Kind::EOF, value: String::new(), pos, quote: 0 }
    }
}

fn keyword(s: &str) -> Option<Kind> {
    Some(match s {
        "return" => Kind::Return,
        "if" => Kind::If,
        "else" => Kind::Else,
        "for" => Kind::For,
        "while" => Kind::While,
        "break" => Kind::Break,
        "continue" => Kind::Continue,
        _ => return None,
    })
}

fn single_char_token(c: u8) -> Option<Kind> {
    Some(match c {
        b'(' => Kind::LParen,
        b')' => Kind::RParen,
        b',' => Kind::Comma,
        b'{' => Kind::LBrace,
        b'}' => Kind::RBrace,
        b'[' => Kind::LBrack,
        b']' => Kind::RBrack,
        b'.' => Kind::Dot,
        b':' => Kind::Colon,
        _ => return None,
    })
}

fn scan_quoted(src: &[u8], mut i: usize, quote: u8) -> (String, usize) {
    i += 1;
    let mut b: Vec<u8> = Vec::new();
    while i < src.len() {
        let c = src[i];
        if c == b'\\' {
            i += 1;
            if i < src.len() {
                b.push(src[i]);
                i += 1;
            }
            continue;
        }
        if c == quote {
            return (String::from_utf8_lossy(&b).into_owned(), i + 1);
        }
        b.push(c);
        i += 1;
    }
    (String::from_utf8_lossy(&b).into_owned(), src.len())
}

/// 三引号字符串 `"""..."""` —— 跨行，转义（对标 Python triple-quote）。
/// 起始 `i` 指向开头的第一个 `"`，返回（内容，闭合 `"""` 之后的字节下标）。
fn scan_triple_quoted(src: &[u8], mut i: usize) -> (String, usize) {
    i += 3;
    let mut b: Vec<u8> = Vec::new();
    while i < src.len() {
        let c = src[i];
        if c == b'\\' {
            i += 1;
            if i < src.len() {
                b.push(src[i]);
                i += 1;
            }
            continue;
        }
        if c == b'"' && i + 2 < src.len() && src[i + 1] == b'"' && src[i + 2] == b'"' {
            return (String::from_utf8_lossy(&b).into_owned(), i + 3);
        }
        b.push(c);
        i += 1;
    }
    (String::from_utf8_lossy(&b).into_owned(), src.len())
}

/// 跨行字面量消费后同步 line/line_start（字面量内部换行不产生 Newline Token，但行号需前进）。
fn advance_line_count(src: &[u8], start: usize, end: usize, line: &mut i32, line_start: &mut usize) {
    for k in start..end {
        if src[k] == b'\n' {
            *line += 1;
            *line_start = k + 1;
        }
    }
}

fn is_token_delim(c: u8) -> bool {
    matches!(
        c,
        b' ' | b'\t'
            | b'\n'
            | b'\r'
            | b';'
            | b','
            | b')'
            | b'('
            | b'{'
            | b'}'
            | b'['
            | b']'
            | b'.'
            | b'+'
            | b'-'
            | b'*'
            | b'%'
            | b'!'
            | b'='
            | b'<'
            | b'>'
            | b'&'
            | b'|'
            | b'^'
            | b':'
    )
}

fn is_abs_path_start(c: u8) -> bool {
    (b'a'..=b'z').contains(&c) || (b'A'..=b'Z').contains(&c) || (b'0'..=b'9').contains(&c) || c == b'_'
}

/// 将整个源扫描为平坦 Token 流，末尾附 EOF。
pub fn scan(src: &str) -> Vec<Token> {
    let src = src.as_bytes();
    let mut tokens: Vec<Token> = Vec::new();
    let mut i = 0usize;
    let mut line = 1i32;
    let mut line_start = 0usize;
    let mut prev_newline = true;

    let pos = |i: usize, line: i32, line_start: usize| Pos {
        line,
        col: (i - line_start) as i32 + 1,
    };

    while i < src.len() {
        let c = src[i];

        // 换行：折叠连续换行
        if c == b'\n' {
            if !prev_newline && !tokens.is_empty() {
                tokens.push(Token {
                    kind: Kind::Newline,
                    value: "\n".to_string(),
                    pos: pos(i, line, line_start),
                    quote: 0,
                });
                prev_newline = true;
            }
            i += 1;
            line += 1;
            line_start = i;
            continue;
        }
        if c == b'\r' {
            i += 1;
            continue;
        }
        // 空白（非换行）
        if c == b' ' || c == b'\t' {
            i += 1;
            continue;
        }
        // ';' — 显式语句分隔符
        if c == b';' {
            if !prev_newline && !tokens.is_empty() {
                tokens.push(Token {
                    kind: Kind::Newline,
                    value: ";".to_string(),
                    pos: pos(i, line, line_start),
                    quote: 0,
                });
                prev_newline = true;
            }
            i += 1;
            continue;
        }
        // # 行注释
        if c == b'#' {
            let p = pos(i, line, line_start);
            let start = i;
            while i < src.len() && src[i] != b'\n' {
                i += 1;
            }
            tokens.push(Token {
                kind: Kind::Comment,
                value: String::from_utf8_lossy(&src[start..i]).into_owned(),
                pos: p,
                quote: 0,
            });
            prev_newline = false;
            continue;
        }

        prev_newline = false;
        let p = pos(i, line, line_start);

        // 反引号原始字符串 `...` — 跨行，零转义（对标 Go raw string）
        if c == b'`' {
            let next = if let Some(end) = find_byte(&src[i + 1..], b'`') {
                tokens.push(Token {
                    kind: Kind::Literal,
                    value: String::from_utf8_lossy(&src[i + 1..i + 1 + end]).into_owned(),
                    pos: p,
                    quote: b'`',
                });
                i + end + 2
            } else {
                tokens.push(Token {
                    kind: Kind::Literal,
                    value: String::from_utf8_lossy(&src[i + 1..]).into_owned(),
                    pos: p,
                    quote: b'`',
                });
                src.len()
            };
            advance_line_count(src, i, next, &mut line, &mut line_start);
            i = next;
            continue;
        }

        // 三引号字符串 """...""" — 跨行，转义（对标 Python triple-quote）
        if c == b'"' && i + 2 < src.len() && src[i + 1] == b'"' && src[i + 2] == b'"' {
            let (val, next) = scan_triple_quoted(src, i);
            tokens.push(Token { kind: Kind::Literal, value: val, pos: p, quote: b'"' });
            advance_line_count(src, i, next, &mut line, &mut line_start);
            i = next;
            continue;
        }

        // 引号字符串
        if c == b'\'' || c == b'"' {
            let (val, next) = scan_quoted(src, i, c);
            let quote = if c == b'"' { b'"' } else { 0 };
            tokens.push(Token { kind: Kind::Literal, value: val, pos: p, quote });
            i = next;
            continue;
        }

        // 左箭头 <-
        if c == b'<' && i + 1 < src.len() && src[i + 1] == b'-' {
            tokens.push(Token { kind: Kind::Arrow, value: "<-".to_string(), pos: p, quote: 0 });
            i += 2;
            continue;
        }
        // 右箭头 ->
        if c == b'-' && i + 1 < src.len() && src[i + 1] == b'>' {
            tokens.push(Token { kind: Kind::Arrow, value: "->".to_string(), pos: p, quote: 0 });
            i += 2;
            continue;
        }

        // 双字符算子
        if i + 1 < src.len() {
            let two = &src[i..i + 2];
            if symbol::scanner_two_char_ops().iter().any(|op| op.as_bytes() == two) {
                tokens.push(Token {
                    kind: Kind::Ident,
                    value: String::from_utf8_lossy(two).into_owned(),
                    pos: p,
                    quote: 0,
                });
                i += 2;
                continue;
            }
        }

        // 赋值算子 =
        if c == b'=' {
            tokens.push(Token { kind: Kind::Arrow, value: "=".to_string(), pos: p, quote: 0 });
            i += 1;
            continue;
        }

        // '/' — // 注释 或 绝对路径字面量 或 除法算子
        if c == b'/' {
            if i + 1 < src.len() && src[i + 1] == b'/' {
                while i < src.len() && src[i] != b'\n' {
                    i += 1;
                }
                continue;
            }
            if i + 1 < src.len() && is_abs_path_start(src[i + 1]) {
                let start = i;
                i += 1;
                while i < src.len() && (!is_token_delim(src[i]) || src[i] == b'.') {
                    i += 1;
                }
                tokens.push(Token {
                    kind: Kind::Literal,
                    value: String::from_utf8_lossy(&src[start..i]).into_owned(),
                    pos: p,
                    quote: 0,
                });
            } else {
                tokens.push(Token { kind: Kind::Ident, value: "/".to_string(), pos: p, quote: 0 });
                i += 1;
            }
            continue;
        }

        // 单字符标点
        if let Some(k) = single_char_token(c) {
            tokens.push(Token { kind: k, value: (c as char).to_string(), pos: p, quote: 0 });
            i += 1;
            continue;
        }

        // 单字符符号算子
        if symbol::scanner_one_char_ops().contains(&c) {
            tokens.push(Token {
                kind: Kind::Ident,
                value: (c as char).to_string(),
                pos: p,
                quote: 0,
            });
            i += 1;
            continue;
        }

        // Unicode 符号算子
        if let Some(op) = symbol::unicode_ops().into_iter().find(|op| src[i..].starts_with(op.as_bytes()))
        {
            tokens.push(Token { kind: Kind::Ident, value: op.clone(), pos: p, quote: 0 });
            i += op.len();
            continue;
        }

        // 数字字面量
        if c.is_ascii_digit() {
            let start = i;
            while i < src.len() && src[i].is_ascii_digit() {
                i += 1;
            }
            if i < src.len() && src[i] == b'.' {
                i += 1;
                while i < src.len() && src[i].is_ascii_digit() {
                    i += 1;
                }
            }
            if i < src.len() && (src[i] == b'e' || src[i] == b'E') {
                i += 1;
                if i < src.len() && (src[i] == b'+' || src[i] == b'-') {
                    i += 1;
                }
                while i < src.len() && src[i].is_ascii_digit() {
                    i += 1;
                }
            }
            tokens.push(Token {
                kind: Kind::Literal,
                value: String::from_utf8_lossy(&src[start..i]).into_owned(),
                pos: p,
                quote: 0,
            });
            continue;
        }

        // 关键字 / 标识符
        let start = i;
        while i < src.len() && !is_token_delim(src[i]) {
            i += 1;
        }
        if i == start {
            i += 1;
            continue;
        }
        let word = String::from_utf8_lossy(&src[start..i]).into_owned();
        if let Some(k) = keyword(&word) {
            tokens.push(Token { kind: k, value: word, pos: p, quote: 0 });
        } else {
            tokens.push(Token { kind: Kind::Ident, value: word, pos: p, quote: 0 });
        }
    }

    tokens.push(Token::eof(pos(i, line, line_start)));
    tokens
}

fn find_byte(s: &[u8], b: u8) -> Option<usize> {
    s.iter().position(|&x| x == b)
}

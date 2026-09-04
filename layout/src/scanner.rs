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
    pub warn: bool, // true = 警告
    pub info: bool, // true = 提示（优先级高于 warn）
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
            format!(
                "{kind}: {src}:{}:{}: {}",
                self.pos.line, self.pos.col, self.message
            )
        } else {
            format!(
                "{kind}: {}:{}: {}",
                self.pos.line, self.pos.col, self.message
            )
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
    pub quote: u8, // 0=无, '"'=转义串 "...", 'r'=原始串 r#"..."#
}

impl Token {
    pub fn eof(pos: Pos) -> Token {
        Token {
            kind: Kind::EOF,
            value: String::new(),
            pos,
            quote: 0,
        }
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
        b':' => Kind::Colon,
        _ => return None,
    })
}

fn escaped_byte(c: u8) -> u8 {
    match c {
        b'n' => b'\n',
        b't' => b'\t',
        b'r' => b'\r',
        b'0' => 0,
        other => other,
    }
}

fn scan_quoted(src: &[u8], mut i: usize, quote: u8) -> (String, usize) {
    i += 1;
    let mut b: Vec<u8> = Vec::new();
    while i < src.len() {
        let c = src[i];
        if c == b'\\' {
            i += 1;
            if i < src.len() {
                b.push(escaped_byte(src[i]));
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

/// Rust 原始字符串 `r"..."` / `r#"..."#` / `r##"..."##` … —— 跨行，零转义。
/// `i` 指向 `r`。成功返回（内容, 闭合之后下标, true）；非 raw 形式返回 (_, i, false)。
fn scan_raw(src: &[u8], i: usize) -> (String, usize, bool) {
    let mut j = i + 1;
    let mut hashes = 0usize;
    while j < src.len() && src[j] == b'#' {
        hashes += 1;
        j += 1;
    }
    if j >= src.len() || src[j] != b'"' {
        return (String::new(), i, false);
    }
    let content_start = j + 1;
    let mut k = content_start;
    while k < src.len() {
        if src[k] == b'"' {
            let mut h = 0;
            while h < hashes && k + 1 + h < src.len() && src[k + 1 + h] == b'#' {
                h += 1;
            }
            if h == hashes {
                let val = String::from_utf8_lossy(&src[content_start..k]).into_owned();
                return (val, k + 1 + hashes, true);
            }
        }
        k += 1;
    }
    (
        String::from_utf8_lossy(&src[content_start..]).into_owned(),
        src.len(),
        true,
    )
}

/// Rust 块注释 `/* ... */`（可嵌套）。`i` 指向第一个 `/`，返回（整段含定界符, 之后下标）。
fn scan_block_comment(src: &[u8], i: usize) -> (String, usize) {
    let mut j = i + 2;
    let mut depth = 1usize;
    while j < src.len() && depth > 0 {
        if j + 1 < src.len() && src[j] == b'/' && src[j + 1] == b'*' {
            depth += 1;
            j += 2;
            continue;
        }
        if j + 1 < src.len() && src[j] == b'*' && src[j + 1] == b'/' {
            depth -= 1;
            j += 2;
            continue;
        }
        j += 1;
    }
    (String::from_utf8_lossy(&src[i..j]).into_owned(), j)
}

/// 跨行字面量消费后同步 line/line_start（字面量内部换行不产生 Newline Token，但行号需前进）。
fn advance_line_count(
    src: &[u8],
    start: usize,
    end: usize,
    line: &mut i32,
    line_start: &mut usize,
) {
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
            | 0xC2 // ·（U+00B7）成员分隔符首字节
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
    (b'a'..=b'z').contains(&c)
        || (b'A'..=b'Z').contains(&c)
        || (b'0'..=b'9').contains(&c)
        || c == b'_'
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
        prev_newline = false;
        let p = pos(i, line, line_start);

        // Rust 原始字符串 r"..." / r#"..."# — 跨行，零转义
        if c == b'r' {
            let (val, next, ok) = scan_raw(src, i);
            if ok {
                tokens.push(Token {
                    kind: Kind::Literal,
                    value: val,
                    pos: p,
                    quote: b'r',
                });
                advance_line_count(src, i, next, &mut line, &mut line_start);
                i = next;
                continue;
            }
        }

        // 引号字符串 "..." — 跨行，转义
        if c == b'\'' || c == b'"' {
            let (val, next) = scan_quoted(src, i, c);
            let quote = if c == b'"' { b'"' } else { 0 };
            tokens.push(Token {
                kind: Kind::Literal,
                value: val,
                pos: p,
                quote,
            });
            advance_line_count(src, i, next, &mut line, &mut line_start);
            i = next;
            continue;
        }

        // 左箭头 <-
        if c == b'<' && i + 1 < src.len() && src[i + 1] == b'-' {
            tokens.push(Token {
                kind: Kind::Arrow,
                value: "<-".to_string(),
                pos: p,
                quote: 0,
            });
            i += 2;
            continue;
        }
        // 右箭头 ->
        if c == b'-' && i + 1 < src.len() && src[i + 1] == b'>' {
            tokens.push(Token {
                kind: Kind::Arrow,
                value: "->".to_string(),
                pos: p,
                quote: 0,
            });
            i += 2;
            continue;
        }

        // 双字符算子
        if i + 1 < src.len() {
            let two = &src[i..i + 2];
            if symbol::scanner_two_char_ops()
                .iter()
                .any(|op| op.as_bytes() == two)
            {
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
            tokens.push(Token {
                kind: Kind::Arrow,
                value: "=".to_string(),
                pos: p,
                quote: 0,
            });
            i += 1;
            continue;
        }

        // '/' — // 行注释 或 /* */ 块注释 或 绝对路径字面量 或 除法算子
        if c == b'/' {
            if i + 1 < src.len() && src[i + 1] == b'/' {
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
                continue;
            }
            if i + 1 < src.len() && src[i + 1] == b'*' {
                let (val, next) = scan_block_comment(src, i);
                tokens.push(Token {
                    kind: Kind::Comment,
                    value: val,
                    pos: p,
                    quote: 0,
                });
                advance_line_count(src, i, next, &mut line, &mut line_start);
                i = next;
                continue;
            }
            if i + 1 < src.len() && is_abs_path_start(src[i + 1]) {
                let start = i;
                i += 1;
                while i < src.len() {
                    // ·（U+00B7）与 .（释放给小数 key）是路径字符；· 后的坐标段 [0,1] 是成员链。
                    if src[i] == 0xC2 && i + 1 < src.len() && src[i + 1] == 0xB7 {
                        i += 2;
                        if i < src.len() && src[i] == b'[' {
                            while i < src.len() && src[i] != b']' {
                                i += 1;
                            }
                            if i < src.len() {
                                i += 1; // 跳过 ]
                            }
                        }
                        continue;
                    }
                    if src[i] == b'.' {
                        i += 1;
                        continue;
                    }
                    if is_token_delim(src[i]) {
                        break;
                    }
                    i += 1;
                }
                tokens.push(Token {
                    kind: Kind::Literal,
                    value: String::from_utf8_lossy(&src[start..i]).into_owned(),
                    pos: p,
                    quote: 0,
                });
            } else {
                tokens.push(Token {
                    kind: Kind::Ident,
                    value: "/".to_string(),
                    pos: p,
                    quote: 0,
                });
                i += 1;
            }
            continue;
        }

        // 成员分隔符 ·（U+00B7，2 字节）
        if c == 0xC2 && i + 1 < src.len() && src[i + 1] == 0xB7 {
            tokens.push(Token {
                kind: Kind::Dot,
                value: "·".to_string(),
                pos: p,
                quote: 0,
            });
            i += 2;
            prev_newline = false;
            continue;
        }

        // 单字符标点
        if let Some(k) = single_char_token(c) {
            tokens.push(Token {
                kind: k,
                value: (c as char).to_string(),
                pos: p,
                quote: 0,
            });
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
        if let Some(op) = symbol::unicode_ops()
            .into_iter()
            .find(|op| src[i..].starts_with(op.as_bytes()))
        {
            tokens.push(Token {
                kind: Kind::Ident,
                value: op.clone(),
                pos: p,
                quote: 0,
            });
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
            tokens.push(Token {
                kind: k,
                value: word,
                pos: p,
                quote: 0,
            });
        } else {
            tokens.push(Token {
                kind: Kind::Ident,
                value: word,
                pos: p,
                quote: 0,
            });
        }
    }

    tokens.push(Token::eof(pos(i, line, line_start)));
    tokens
}

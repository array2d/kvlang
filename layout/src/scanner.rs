
use std::fmt;

use super::symbol;

#[derive(Clone, Copy, Default)]
pub struct Pos {
    pub line: i32, 
    pub col: i32,  
}

#[derive(Clone)]
pub struct Diagnostic {
    pub pos: Pos,
    pub message: String,
    pub warn: bool, 
    pub info: bool, 
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
    pub quote: u8, 
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

fn scan_quoted(src: &[u8], mut i: usize, quote: u8) -> (String, usize, bool) {
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
            return (String::from_utf8_lossy(&b).into_owned(), i + 1, true);
        }
        b.push(c);
        i += 1;
    }
    (String::from_utf8_lossy(&b).into_owned(), src.len(), false)
}

fn scan_raw(src: &[u8], i: usize) -> (String, usize, bool, bool) {
    let mut j = i + 1;
    let mut hashes = 0usize;
    while j < src.len() && src[j] == b'#' {
        hashes += 1;
        j += 1;
    }
    if j >= src.len() || src[j] != b'"' {
        return (String::new(), i, false, false);
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
                return (val, k + 1 + hashes, true, true);
            }
        }
        k += 1;
    }
    (
        String::from_utf8_lossy(&src[content_start..]).into_owned(),
        src.len(),
        true,
        false,
    )
}

fn scan_block_comment(src: &[u8], i: usize) -> (String, usize, bool) {
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
    (
        String::from_utf8_lossy(&src[i..j]).into_owned(),
        j,
        depth == 0,
    )
}

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

pub(crate) fn is_token_delim_at(src: &[u8], i: usize) -> bool {
    match src[i] {
        0xC2 => src.get(i + 1) == Some(&0xB7),
        c => is_ascii_token_delim(c),
    }
}

pub(crate) fn has_token_delim(s: &str) -> bool {
    let b = s.as_bytes();
    (0..b.len()).any(|i| is_token_delim_at(b, i))
}

fn is_ascii_token_delim(c: u8) -> bool {
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

pub fn scan(src: &str) -> Vec<Token> {
    scan_with_diags(src).0
}

fn lex_error(pos: Pos, message: &str) -> Diagnostic {
    Diagnostic {
        pos,
        message: message.to_string(),
        warn: false,
        info: false,
        source: String::new(),
        src_file: String::new(),
        src_name: String::new(),
    }
}

pub fn scan_with_diags(src: &str) -> (Vec<Token>, Vec<Diagnostic>) {
    let src = src.as_bytes();
    let mut diags: Vec<Diagnostic> = Vec::new();
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
        
        if c == b' ' || c == b'\t' {
            i += 1;
            continue;
        }
        
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

        if c == b'r' {
            let (val, next, ok, closed) = scan_raw(src, i);
            if ok {
                if !closed {
                    diags.push(lex_error(
                        p,
                        "unterminated raw string literal (missing closing `\"#…`)",
                    ));
                }
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

        if c == b'\'' || c == b'"' {
            let (val, next, closed) = scan_quoted(src, i, c);
            if !closed {
                diags.push(lex_error(p, "unterminated string literal"));
            }
            
            let quote = c;
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

        if c == b'#' {
            let start = i;
            i += 1;
            while i < src.len() && !is_token_delim_at(src, i) {
                i += 1;
            }
            
            if i == start + 1 {
                diags.push(lex_error(
                    p,
                    "空注释：`#` 后必须紧跟一个词（如 `#acc`）；`#` 与注释体之间不能有空白",
                ));
            }
            tokens.push(Token {
                kind: Kind::Comment,
                value: String::from_utf8_lossy(&src[start..i]).into_owned(),
                pos: p,
                quote: 0,
            });
            continue;
        }

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
                let (val, next, closed) = scan_block_comment(src, i);
                if !closed {
                    diags.push(lex_error(p, "unterminated block comment (missing `*/`)"));
                }
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
                    
                    if src[i] == 0xC2 && i + 1 < src.len() && src[i + 1] == 0xB7 {
                        
                        if i + 2 < src.len() && src[i + 2] == b'*' {
                            break;
                        }
                        i += 2;
                        if i < src.len() && src[i] == b'[' {
                            while i < src.len() && src[i] != b']' {
                                i += 1;
                            }
                            if i < src.len() {
                                i += 1; 
                            }
                        }
                        continue;
                    }
                    if src[i] == b'.' {
                        i += 1;
                        continue;
                    }
                    if is_token_delim_at(src, i) {
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

        if let Some(op) = symbol::unicode_ops()
            .iter()
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

        if c.is_ascii_digit() {
            let start = i;
            while i < src.len() && src[i].is_ascii_digit() {
                i += 1;
            }
            
            if i < src.len() && src[i] == b'.' && !(i + 1 < src.len() && src[i + 1] == b'.') {
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

        let start = i;
        while i < src.len() && !is_token_delim_at(src, i) {
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
    (tokens, diags)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn toks(src: &str) -> Vec<String> {
        scan(src)
            .into_iter()
            .filter(|t| t.kind != Kind::EOF)
            .map(|t| {
                let k = match t.kind {
                    Kind::Ident => "I",
                    Kind::Dot => "D",
                    _ => "?",
                };
                format!("{k}:{}", t.value)
            })
            .collect()
    }

    #[test]
    fn dbg_deref_paren_tokens() {
        eprintln!("TOKENS {:?}", toks("(*r)[*i]"));
    }

    #[test]
    fn only_c2_b7_is_member_delimiter() {
        
        assert_eq!(toks("温度°C"), vec!["I:温度°C"]);
        assert_eq!(toks("µs"), vec!["I:µs"]);
        assert_eq!(toks("a·b"), vec!["I:a", "D:·", "I:b"]);
        assert_eq!(toks("a·*k"), vec!["I:a", "D:·", "I:*", "I:k"]);
        
        assert!(!has_token_delim("点°"));
        assert!(has_token_delim("a·b"));
    }

    #[test]
    fn hash_starts_inline_comment_ending_at_delimiter() {
        let cs = |src: &str| -> Vec<String> {
            scan(src)
                .into_iter()
                .filter(|t| t.kind == Kind::Comment)
                .map(|t| t.value)
                .collect()
        };
        
        assert_eq!(cs("(*int64 #acc)"), vec!["#acc"]);
        assert_eq!(
            toks("f(*int64 #delta) -> ()"),
            vec!["I:f", "?:(", "I:*", "I:int64", "?:#delta", "?:)", "?:->", "?:(", "?:)"]
        );
        
        assert_eq!(cs("#acc\nx = 1\n"), vec!["#acc"]);
        assert_eq!(cs("# trailing note\n"), vec!["#"]);
        assert_eq!(cs("println(\"guess #\", n)"), Vec::<String>::new());
        
        assert_eq!(cs("x = r#\"a # b\"#"), Vec::<String>::new());
        assert_eq!(cs("x = r####\"a # b\"# c\"####"), Vec::<String>::new());
        
        assert_eq!(cs("// # one\nx = 1\n"), vec!["// # one"]);
        assert_eq!(
            cs("/* # one\n # two */ x = 1\n"),
            vec!["/* # one\n # two */"]
        );
    }

    #[test]
    fn unterminated_literals_are_errors() {
        for (src, want) in [
            ("x = \"abc\nprintln(x)\n", "unterminated string literal"),
            ("x = 'abc\n", "unterminated string literal"),
            ("x = r#\"abc\"\n", "unterminated raw string literal"),
            ("/* never closed\nx = 1\n", "unterminated block comment"),
            (
                "/* a /* nested */ still open\n",
                "unterminated block comment",
            ),
        ] {
            let (_, d) = scan_with_diags(src);
            assert_eq!(d.len(), 1, "{src:?}");
            assert!(!d[0].warn && !d[0].info, "{src:?} must be error-level");
            assert!(d[0].message.starts_with(want), "{src:?}: {}", d[0].message);
            assert_eq!((d[0].pos.line, d[0].pos.col > 0), (1, true), "{src:?}");
        }
        for src in [
            "x = \"a\\\"b\"\n",
            "x = r#\"a\"b\"#\n",
            "/* a /* b */ c */\nx = 1\n",
        ] {
            assert!(scan_with_diags(src).1.is_empty(), "{src:?}");
        }
    }
}

use crate::store::Store;
use std::collections::HashSet;
use std::io::{self, Read, Write};
use std::process::Command;
use std::thread::sleep;
use std::time::Duration;

const ESCAPE: char = '\u{1b}';
const DIVIDER: char = '│';
const HEAD_COLUMN_WIDTH: usize = 20;
const ATTR_COLUMN_WIDTH: usize = 20;
const COLUMN_GAP: usize = 1;

struct Row {
    key: String,
    name: String,
    depth: usize,
    is_directory: bool,
}

struct Panes {
    summary: Vec<String>,
    head: Vec<String>,
    body: Vec<String>,
    attributes: Vec<String>,
}

struct Terminal {
    active: bool,
}

impl Terminal {
    fn enter() -> io::Result<Terminal> {
        stty(&["raw", "-echo", "min", "0", "time", "1"])?;
        let mut output = io::stdout();
        write!(output, "{ESCAPE}[?1049h{ESCAPE}[?25l{ESCAPE}[2J")?;
        output.flush()?;
        Ok(Terminal { active: true })
    }

    fn size() -> (usize, usize) {
        if let Some(size) = size_from_command("stty", &["size"]) {
            return size;
        }
        let rows = size_from_command("tput", &["lines"]).map(|size| size.0);
        let columns = size_from_command("tput", &["cols"]).map(|size| size.0);
        let rows = rows.or_else(|| env_number("LINES")).unwrap_or(24);
        let columns = columns.or_else(|| env_number("COLUMNS")).unwrap_or(80);
        (rows, columns)
    }
}

impl Drop for Terminal {
    fn drop(&mut self) {
        if !self.active {
            return;
        }
        let mut output = io::stdout();
        let _ = write!(output, "{ESCAPE}[?25h{ESCAPE}[?1049l");
        let _ = output.flush();
        let _ = stty(&["sane"]);
    }
}

fn size_from_command(command: &str, arguments: &[&str]) -> Option<(usize, usize)> {
    let output = Command::new(command).args(arguments).output().ok()?;
    let text = String::from_utf8_lossy(&output.stdout);
    let mut parts = text.split_whitespace();
    let first = parts.next()?.parse().ok()?;
    let second = parts.next().and_then(|value| value.parse().ok());
    Some((first, second.unwrap_or(first)))
}

fn env_number(name: &str) -> Option<usize> {
    std::env::var(name).ok().and_then(|value| value.parse().ok())
}

fn stty(arguments: &[&str]) -> io::Result<()> {
    let status = Command::new("stty").args(arguments).status()?;
    if status.success() {
        Ok(())
    } else {
        Err(io::Error::other("stty failed"))
    }
}

pub fn run(store: &Store, data_source_name: &str) -> i32 {
    let terminal = match Terminal::enter() {
        Ok(terminal) => terminal,
        Err(error) => {
            eprintln!("kvi: cannot enter terminal mode: {error}");
            return 1;
        }
    };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        event_loop(store, data_source_name)
    }));
    drop(terminal);
    match result {
        Ok(code) => code,
        Err(_) => 1,
    }
}

fn event_loop(store: &Store, data_source_name: &str) -> i32 {
    let mut expanded: HashSet<String> = HashSet::new();
    expanded.insert("/".to_string());
    let mut selected: usize = 0;
    let mut status = String::new();
    let mut body_cross_line = false;
    let mut key_pane_percent: usize = 40;
    let mut maximize_key_pane = false;
    let mut input = [0u8; 16];
    loop {
        let rows = flatten(store, &expanded);
        if selected >= rows.len() {
            selected = rows.len().saturating_sub(1);
        }
        draw(
            store,
            data_source_name,
            &rows,
            selected,
            &status,
            body_cross_line,
            key_pane_percent,
            maximize_key_pane,
        );
        let count = match io::stdin().read(&mut input) {
            Ok(count) => count,
            Err(_) => 0,
        };
        if count == 0 {
            sleep(Duration::from_millis(10));
            continue;
        }
        for key in parse_keys(&input[..count]) {
            match key {
                Key::Quit => return 0,
                Key::Down => {
                    if selected + 1 < rows.len() {
                        selected += 1;
                    }
                }
                Key::Up => selected = selected.saturating_sub(1),
                Key::Top => selected = 0,
                Key::Bottom => selected = rows.len().saturating_sub(1),
                Key::Expand => {
                    if let Some(row) = rows.get(selected) {
                        if row.is_directory {
                            expanded.insert(row.key.clone());
                        }
                    }
                }
                Key::Collapse => {
                    if let Some(row) = rows.get(selected) {
                        if row.is_directory && expanded.contains(&row.key) {
                            expanded.remove(&row.key);
                        } else if let Some(parent) = parent_directory(&row.key) {
                            if let Some(index) = rows.iter().position(|entry| entry.key == parent) {
                                selected = index;
                            }
                        }
                    }
                }

                Key::Toggle => {
                    if let Some(row) = rows.get(selected) {
                        if row.is_directory {
                            if expanded.contains(&row.key) {
                                expanded.remove(&row.key);
                            } else {
                                expanded.insert(row.key.clone());
                            }
                        } else {
                            status = row.key.clone();
                        }
                    }
                }
                Key::ToggleBody => {
                    body_cross_line = !body_cross_line;
                    status = if body_cross_line {
                        "body: cross-line".to_string()
                    } else {
                        "body: column".to_string()
                    };
                }
                Key::ToggleMaximize => {
                    maximize_key_pane = !maximize_key_pane;
                    status = if maximize_key_pane {
                        "key pane: full width".to_string()
                    } else {
                        "key pane: split".to_string()
                    };
                }
                Key::GrowKeyPane => {
                    key_pane_percent = (key_pane_percent + 5).min(80);
                }
                Key::ShrinkKeyPane => {
                    key_pane_percent = key_pane_percent.saturating_sub(5).max(20);
                }
                Key::Refresh => status = "refreshed".to_string(),
                Key::Other => {}
            }
        }
    }
}

fn flatten(store: &Store, expanded: &HashSet<String>) -> Vec<Row> {
    let mut rows = Vec::new();
    collect(store, expanded, "/", "/", 0, &mut rows);
    rows
}

fn collect(
    store: &Store,
    expanded: &HashSet<String>,
    directory: &str,
    name: &str,
    depth: usize,
    rows: &mut Vec<Row>,
) {
    rows.push(Row {
        key: directory.to_string(),
        name: name.to_string(),
        depth,
        is_directory: true,
    });
    if !expanded.contains(directory) {
        return;
    }
    let children = match store.list(directory) {
        Ok(children) => children,
        Err(_) => return,
    };
    for child in children {
        let key = format!("{directory}{child}");
        if is_directory_name(&child) {
            collect(store, expanded, &key, &child, depth + 1, rows);
        } else {
            rows.push(Row {
                key,
                name: child,
                depth: depth + 1,
                is_directory: false,
            });
        }
    }
}

fn is_directory_name(name: &str) -> bool {
    name.ends_with('/') || name.ends_with('·')
}

fn parent_directory(key: &str) -> Option<String> {
    let mut stripped = key;
    loop {
        match stripped.chars().last() {
            Some('/') => stripped = &stripped[..stripped.len() - 1],
            Some('·') => stripped = &stripped[..stripped.len() - 2],
            _ => break,
        }
    }
    match stripped.rfind('/') {
        Some(index) => Some(stripped[..=index].to_string()),
        None => stripped
            .rfind('·')
            .map(|index| stripped[..index + 2].to_string()),
    }
}

fn draw(
    store: &Store,
    data_source_name: &str,
    rows: &[Row],
    selected: usize,
    status: &str,
    body_cross_line: bool,
    key_pane_percent: usize,
    maximize_key_pane: bool,
) {
    let (height, width) = Terminal::size();
    let left_width = if maximize_key_pane {
        width
    } else {
        (width * key_pane_percent / 100).clamp(20, width.saturating_sub(24).max(20))
    };
    let right_width = width.saturating_sub(left_width + 1).max(1);
    let panes = rows
        .get(selected)
        .map(|row| build_panes(store, row))
        .unwrap_or(Panes {
            summary: Vec::new(),
            head: Vec::new(),
            body: Vec::new(),
            attributes: Vec::new(),
        });
    let cross_line = body_cross_line;
    let mut screen = String::new();
    screen.push_str(&format!(
        "{ESCAPE}[H{ESCAPE}[2J{ESCAPE}[1;1H{ESCAPE}[7mkvi  {data_source_name}{ESCAPE}[0m"
    ));
    let content_height = height.saturating_sub(2);
    for index in 0..content_height {
        let row_number = index + 2;
        let key_text = rows.get(index).map(row_text).unwrap_or_default();
        let key_text = fit(&key_text, left_width);
        if index == selected {
            screen.push_str(&format!(
                "{ESCAPE}[{row_number};1H{ESCAPE}[7m{}{ESCAPE}[0m",
                pad(&key_text, left_width)
            ));
        } else if let Some(row) = rows.get(index) {
            let text = pad(&key_text, left_width);
            if row.is_directory {
                screen.push_str(&format!(
                    "{ESCAPE}[{row_number};1H{ESCAPE}[1m{text}{ESCAPE}[0m"
                ));
            } else {
                screen.push_str(&format!("{ESCAPE}[{row_number};1H{text}"));
            }
        }
        if maximize_key_pane {
            continue;
        }
        screen.push_str(&format!(
            "{ESCAPE}[{row_number};{}H{DIVIDER}",
            left_width + 1
        ));
        if cross_line {
            draw_cross_line(
                &mut screen,
                &panes,
                index,
                row_number,
                left_width + 2,
                right_width,
            );
        } else {
            draw_columns(
                &mut screen,
                &panes,
                index,
                row_number,
                left_width + 2,
                right_width,
            );
        }
    }
    let footer = format!(
        "j/k 移动  h/l 折叠/展开  Enter 切换  v body 跨行/分栏  z 键区全宽  </> 调宽  r 刷新  q 退出{}",
        if status.is_empty() {
            String::new()
        } else {
            format!("  ·  {status}")
        }
    );
    screen.push_str(&format!(
        "{ESCAPE}[{height};1H{ESCAPE}[7m{}{ESCAPE}[0m",
        fit(&footer, width)
    ));
    let mut output = io::stdout();
    let _ = write!(output, "{screen}");
    let _ = output.flush();
}

fn draw_columns(
    screen: &mut String,
    panes: &Panes,
    index: usize,
    row_number: usize,
    start_column: usize,
    width: usize,
) {
    let head_width = HEAD_COLUMN_WIDTH.min(width / 3);
    let attribute_width = ATTR_COLUMN_WIDTH.min(width / 3);
    let body_width = width.saturating_sub(
        head_width + attribute_width + COLUMN_GAP * 2,
    );
    let body_column = start_column + head_width + COLUMN_GAP;
    let attribute_column = body_column + body_width + COLUMN_GAP;
    let head_line = panes.head.get(index).map(String::as_str).unwrap_or("");
    let body_line = panes.body.get(index).map(String::as_str).unwrap_or("");
    let attribute_line = panes.attributes.get(index).map(String::as_str).unwrap_or("");
    screen.push_str(&format!(
        "{ESCAPE}[{row_number};{start_column}H{}",
        fit(&pad(head_line, head_width), head_width)
    ));
    screen.push_str(&format!(
        "{ESCAPE}[{row_number};{}H{DIVIDER}",
        body_column - 1
    ));
    screen.push_str(&format!(
        "{ESCAPE}[{row_number};{body_column}H{}",
        fit(&pad(body_line, body_width), body_width)
    ));
    screen.push_str(&format!(
        "{ESCAPE}[{row_number};{}H{DIVIDER}",
        attribute_column - 1
    ));
    screen.push_str(&format!(
        "{ESCAPE}[{row_number};{attribute_column}H{}",
        fit(attribute_line, attribute_width)
    ));
}

fn draw_cross_line(
    screen: &mut String,
    panes: &Panes,
    index: usize,
    row_number: usize,
    start_column: usize,
    width: usize,
) {
    let summary_count = panes.summary.len();
    let line = if index < summary_count {
        panes.summary[index].as_str()
    } else if index == summary_count {
        ""
    } else {
        panes
            .body
            .get(index - summary_count - 1)
            .map(String::as_str)
            .unwrap_or("")
    };
    screen.push_str(&format!(
        "{ESCAPE}[{row_number};{start_column}H{}",
        fit(line, width)
    ));
}

fn row_text(row: &Row) -> String {
    format!("{}{}", "  ".repeat(row.depth), row.name)
}

fn build_panes(store: &Store, row: &Row) -> Panes {
    let mut head = Vec::new();
    let mut body = Vec::new();
    let mut attributes = Vec::new();
    let mut summary = Vec::new();
    summary.push(format!("key: {}", row.key));
    if row.is_directory {
        match store.list(&row.key) {
            Ok(children) => body.push(format!("directory, {} entries", children.len())),
            Err(status) => body.push(format!("list failed: {status}")),
        }
        return Panes {
            summary,
            head,
            body,
            attributes,
        };
    }
    let wire = match store.resolve(&row.key) {
        Ok(Some(wire)) => wire,
        Ok(None) => {
            body.push("(missing)".to_string());
            return Panes {
                summary,
                head,
                body,
                attributes,
            };
        }
        Err(status) => {
            body.push(format!("resolve failed: {status}"));
            return Panes {
                summary,
                head,
                body,
                attributes,
            };
        }
    };
    let (body_length, attribute_length) = Store::head_fields(&wire);
    let padding_is_zero = wire[10..16].iter().all(|byte| *byte == 0);
    head.push(format!("bodylen {body_length}"));
    head.push(format!("attrslen {attribute_length}"));
    head.push(format!("total {}", wire.len()));
    head.push(format!(
        "padding {}",
        if padding_is_zero { "zero" } else { "NONZERO" }
    ));
    summary.push(format!(
        "bodylen={body_length} attrslen={attribute_length} total={} padding={}",
        wire.len(),
        if padding_is_zero { "zero" } else { "NONZERO" }
    ));
    let language_type = store
        .langtype(&wire)
        .map(|bytes| String::from_utf8_lossy(&bytes).into_owned())
        .unwrap_or_else(|| "(none)".to_string());
    summary.push(format!("langtype={language_type}"));
    body.push(format!("langtype: {language_type}"));
    let stored_body = Store::body(&wire);
    if stored_body.is_empty() {
        body.push("(empty body)".to_string());
    } else if body_is_text(stored_body) {
        body.extend(
            String::from_utf8_lossy(stored_body)
                .lines()
                .map(|line| line.to_string()),
        );
    } else {
        body.extend(hex_lines(stored_body, 8));
    }
    attributes.extend(attribute_lines(&wire, body_length, attribute_length));
    Panes {
        summary,
        head,
        body,
        attributes,
    }
}

fn attribute_lines(wire: &[u8], body_length: u64, attribute_length: u16) -> Vec<String> {
    let mut lines = Vec::new();
    let mut cursor = 16 + body_length as usize;
    let end = cursor + attribute_length as usize;
    if cursor >= end {
        lines.push("(none)".to_string());
        return lines;
    }
    while cursor + 3 <= end {
        let tag = wire[cursor];
        let length = u16::from_le_bytes([wire[cursor + 1], wire[cursor + 2]]) as usize;
        if cursor + 3 + length > end {
            lines.push("overflow".to_string());
            break;
        }
        let data = &wire[cursor + 3..cursor + 3 + length];
        let text = String::from_utf8_lossy(data);
        if text.chars().all(|character| !character.is_control()) {
            lines.push(format!("tag {tag} len {length} {text}"));
        } else {
            let hex: String = data.iter().map(|byte| format!("{byte:02x}")).collect();
            lines.push(format!("tag {tag} len {length} {hex}"));
        }
        cursor += 3 + length;
    }
    lines
}

fn body_is_text(body: &[u8]) -> bool {
    body.iter()
        .all(|byte| *byte == b'\n' || *byte == b'\t' || (0x20..=0x7e).contains(byte))
}

fn hex_lines(body: &[u8], bytes_per_line: usize) -> Vec<String> {
    let mut lines = Vec::new();
    for (offset, chunk) in body.chunks(bytes_per_line).enumerate() {
        let mut line = format!("{:08x} ", offset * bytes_per_line);
        for index in 0..bytes_per_line {
            match chunk.get(index) {
                Some(byte) => line.push_str(&format!("{byte:02x} ")),
                None => line.push_str("   "),
            }
        }
        line.push_str(" |");
        for byte in chunk {
            line.push(if (0x20..=0x7e).contains(byte) {
                *byte as char
            } else {
                '.'
            });
        }
        line.push('|');
        lines.push(line);
    }
    lines
}

fn character_width(character: char) -> usize {
    let code = character as u32;
    let wide = (0x1100..=0x115f).contains(&code)
        || (0x2e80..=0xa4cf).contains(&code)
        || (0xac00..=0xd7a3).contains(&code)
        || (0xf900..=0xfaff).contains(&code)
        || (0xfe30..=0xfe4f).contains(&code)
        || (0xff00..=0xff60).contains(&code)
        || (0xffe0..=0xffe6).contains(&code)
        || (0x20000..=0x3fffd).contains(&code);
    if wide {
        2
    } else {
        1
    }
}

fn fit(text: &str, width: usize) -> String {
    let mut result = String::new();
    let mut used = 0;
    for character in text.chars() {
        let character_width = character_width(character);
        if used + character_width > width {
            break;
        }
        result.push(character);
        used += character_width;
    }
    result
}

fn pad(text: &str, width: usize) -> String {
    let used: usize = text.chars().map(character_width).sum();
    format!("{text}{}", " ".repeat(width.saturating_sub(used)))
}

enum Key {
    Quit,
    Up,
    Down,
    Expand,
    Collapse,
    Toggle,
    ToggleBody,
    ToggleMaximize,
    GrowKeyPane,
    ShrinkKeyPane,
    Refresh,
    Top,
    Bottom,
    Other,
}

fn parse_keys(bytes: &[u8]) -> Vec<Key> {
    let mut keys = Vec::new();
    let mut index = 0;
    while index < bytes.len() {
        let byte = bytes[index];
        if byte == 0x1b {
            if bytes.get(index + 1) == Some(&b'[') {
                match bytes.get(index + 2) {
                    Some(b'A') => keys.push(Key::Up),
                    Some(b'B') => keys.push(Key::Down),
                    Some(b'C') => keys.push(Key::Expand),
                    Some(b'D') => keys.push(Key::Collapse),
                    _ => keys.push(Key::Other),
                }
                index += 3;
                continue;
            }
            keys.push(Key::Other);
            index += 1;
            continue;
        }
        keys.push(match byte {
            b'q' => Key::Quit,
            0x03 => Key::Quit,
            b'k' => Key::Up,
            b'j' => Key::Down,
            b'h' => Key::Collapse,
            b'l' => Key::Expand,
            b'g' => Key::Top,
            b'G' => Key::Bottom,
            b'\r' | b'\n' => Key::Toggle,
            b'v' => Key::ToggleBody,
            b'z' => Key::ToggleMaximize,
            b'>' => Key::GrowKeyPane,
            b'<' => Key::ShrinkKeyPane,
            b'r' => Key::Refresh,
            _ => Key::Other,
        });
        index += 1;
    }
    keys
}

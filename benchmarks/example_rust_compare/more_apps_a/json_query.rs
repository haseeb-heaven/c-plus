// Safe-Rust model of examples/expanded/apps_2/json_query.cp
// No unsafe, no FFI, no external crates. Deterministic JSON field extraction.
// Build: rustc -O --edition 2021 json_query.rs -o json_query
//
// The C+ original parses with yyjson via stdlib/json.hp (depth budget 8).
// Here a small recursive-descent parser over &str models the same observable
// behaviour for the fixed inputs: object keys, array indices, strings, bools
// and integers.  Refusal tokens and exit codes match the original.

const JQ_MAX_DEPTH: usize = 8;
const JQ_PATH_CAP: usize = 128;
const JQ_SEG_CAP: usize = 64;
const JQ_MAX_SEGS: usize = 8;

#[derive(Clone, Debug)]
enum Json {
    Null,
    Bool(bool),
    Int(i64),
    Float(f64),
    Str(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}

impl Json {
    fn member(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Object(entries) => entries.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }
    fn at(&self, index: usize) -> Option<&Json> {
        match self {
            Json::Array(items) => items.get(index),
            _ => None,
        }
    }
}

struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn new(text: &'a str) -> Self {
        Self {
            bytes: text.as_bytes(),
            pos: 0,
        }
    }

    fn ws(&mut self) {
        while self.pos < self.bytes.len()
            && matches!(self.bytes[self.pos], b' ' | b'\t' | b'\n' | b'\r')
        {
            self.pos += 1;
        }
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn parse_value(&mut self, depth: usize) -> Result<Json, ()> {
        if depth > JQ_MAX_DEPTH {
            return Err(());
        }
        self.ws();
        match self.peek() {
            Some(b'{') => self.parse_object(depth),
            Some(b'[') => self.parse_array(depth),
            Some(b'"') => Ok(Json::Str(self.parse_string()?)),
            Some(b't') => {
                self.literal(b"true")?;
                Ok(Json::Bool(true))
            }
            Some(b'f') => {
                self.literal(b"false")?;
                Ok(Json::Bool(false))
            }
            Some(b'n') => {
                self.literal(b"null")?;
                Ok(Json::Null)
            }
            Some(b'-') | Some(b'0'..=b'9') => self.parse_number(),
            _ => Err(()),
        }
    }

    fn literal(&mut self, word: &[u8]) -> Result<(), ()> {
        if self.bytes.get(self.pos..self.pos + word.len()) == Some(word) {
            self.pos += word.len();
            Ok(())
        } else {
            Err(())
        }
    }

    fn parse_string(&mut self) -> Result<String, ()> {
        if self.peek() != Some(b'"') {
            return Err(());
        }
        self.pos += 1;
        let mut out = String::new();
        while self.pos < self.bytes.len() {
            let c = self.bytes[self.pos];
            self.pos += 1;
            match c {
                b'"' => return Ok(out),
                b'\\' => {
                    let esc = *self.bytes.get(self.pos).ok_or(())?;
                    self.pos += 1;
                    match esc {
                        b'"' => out.push('"'),
                        b'\\' => out.push('\\'),
                        b'/' => out.push('/'),
                        b'b' => out.push('\u{0008}'),
                        b'f' => out.push('\u{000c}'),
                        b'n' => out.push('\n'),
                        b'r' => out.push('\r'),
                        b't' => out.push('\t'),
                        b'u' => {
                            let hex = self
                                .bytes
                                .get(self.pos..self.pos + 4)
                                .ok_or(())?;
                            let code = std::str::from_utf8(hex)
                                .ok()
                                .and_then(|s| u16::from_str_radix(s, 16).ok())
                                .ok_or(())?;
                            self.pos += 4;
                            out.push(char::from_u32(code as u32).unwrap_or('\u{fffd}'));
                        }
                        _ => return Err(()),
                    }
                }
                0..=0x1f => return Err(()),
                _ => {
                    // Rebuild the UTF-8 byte sequence for this non-ASCII byte.
                    let start = self.pos - 1;
                    let width = if c < 0x80 {
                        1
                    } else if c < 0xe0 {
                        2
                    } else if c < 0xf0 {
                        3
                    } else {
                        4
                    };
                    let end = (start + width).min(self.bytes.len());
                    let s = std::str::from_utf8(&self.bytes[start..end]).map_err(|_| ())?;
                    out.push_str(s);
                    self.pos = end;
                }
            }
        }
        Err(())
    }

    fn parse_number(&mut self) -> Result<Json, ()> {
        let start = self.pos;
        if self.peek() == Some(b'-') {
            self.pos += 1;
        }
        if self.peek() == Some(b'0') {
            self.pos += 1;
        } else if matches!(self.peek(), Some(b'1'..=b'9')) {
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.pos += 1;
            }
        } else {
            return Err(());
        }
        let mut is_float = false;
        if self.peek() == Some(b'.') {
            is_float = true;
            self.pos += 1;
            if !matches!(self.peek(), Some(b'0'..=b'9')) {
                return Err(());
            }
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.pos += 1;
            }
        }
        if matches!(self.peek(), Some(b'e') | Some(b'E')) {
            is_float = true;
            self.pos += 1;
            if matches!(self.peek(), Some(b'+') | Some(b'-')) {
                self.pos += 1;
            }
            if !matches!(self.peek(), Some(b'0'..=b'9')) {
                return Err(());
            }
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.pos += 1;
            }
        }
        let s = std::str::from_utf8(&self.bytes[start..self.pos]).map_err(|_| ())?;
        if is_float {
            Ok(Json::Float(s.parse::<f64>().map_err(|_| ())?))
        } else {
            s.parse::<i64>()
                .map(Json::Int)
                .or_else(|_| s.parse::<f64>().map(Json::Float).map_err(|_| ()))
        }
    }

    fn parse_array(&mut self, depth: usize) -> Result<Json, ()> {
        self.pos += 1; // '['
        let mut items = Vec::new();
        self.ws();
        if self.peek() == Some(b']') {
            self.pos += 1;
            return Ok(Json::Array(items));
        }
        loop {
            items.push(self.parse_value(depth + 1)?);
            self.ws();
            match self.peek() {
                Some(b',') => {
                    self.pos += 1;
                    self.ws();
                }
                Some(b']') => {
                    self.pos += 1;
                    return Ok(Json::Array(items));
                }
                _ => return Err(()),
            }
        }
    }

    fn parse_object(&mut self, depth: usize) -> Result<Json, ()> {
        self.pos += 1; // '{'
        let mut entries = Vec::new();
        self.ws();
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Ok(Json::Object(entries));
        }
        loop {
            self.ws();
            let key = self.parse_string()?;
            self.ws();
            if self.peek() != Some(b':') {
                return Err(());
            }
            self.pos += 1;
            let value = self.parse_value(depth + 1)?;
            entries.push((key, value));
            self.ws();
            match self.peek() {
                Some(b',') => {
                    self.pos += 1;
                }
                Some(b'}') => {
                    self.pos += 1;
                    return Ok(Json::Object(entries));
                }
                _ => return Err(()),
            }
        }
    }
}

fn refuse(reason: &str) -> i32 {
    println!("JSON error {}", reason);
    1
}

// Mirror json_input_within_budget's container-open scan (depth cap 8).
fn within_budget(text: &str) -> bool {
    if text.is_empty() {
        return false;
    }
    let mut depth = 0usize;
    let mut in_string = false;
    let mut escaped = false;
    for ch in text.bytes() {
        if in_string {
            if escaped {
                escaped = false;
            } else if ch == b'\\' {
                escaped = true;
            } else if ch == b'"' {
                in_string = false;
            }
            continue;
        }
        if ch == b'"' {
            in_string = true;
        } else if ch == b'{' || ch == b'[' {
            depth += 1;
            if depth > JQ_MAX_DEPTH {
                return false;
            }
        } else if (ch == b'}' || ch == b']') && depth > 0 {
            depth -= 1;
        }
    }
    true
}

fn parse_index(seg: &str) -> Option<usize> {
    if seg.is_empty() || !seg.bytes().all(|c| c.is_ascii_digit()) {
        return None;
    }
    let v = seg.parse::<i64>().ok()?;
    if v < 0 || v > i32::MAX as i64 {
        return None;
    }
    Some(v as usize)
}

fn emit(view: &Json, path: &str) -> i32 {
    match view {
        Json::Str(s) => {
            println!("JSON value {} {}", path, s);
            0
        }
        Json::Bool(b) => {
            println!("JSON value {} {}", path, if *b { "true" } else { "false" });
            0
        }
        Json::Int(i) => {
            println!("JSON value {} {}", path, i);
            0
        }
        _ => refuse("unsupported-type"),
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        println!("JSON error usage");
        std::process::exit(2);
    }
    let text = &args[1];
    let path = &args[2];

    if path.is_empty() || path.len() + 1 > JQ_PATH_CAP {
        std::process::exit(refuse("bad-path"));
    }
    let segments: Vec<&str> = path.split('.').collect();
    if segments.len() > JQ_MAX_SEGS {
        std::process::exit(refuse("path-too-deep"));
    }
    if segments.iter().any(|s| s.is_empty() || s.len() + 1 > JQ_SEG_CAP) {
        std::process::exit(refuse("bad-path"));
    }

    if !within_budget(text) {
        // A depth overflow and a malformed string both fail the budget scan;
        // distinguish them the way json_parse_budget reports err.code().
        let mut depth = 0usize;
        let mut in_string = false;
        let mut escaped = false;
        let mut over_depth = false;
        for ch in text.bytes() {
            if in_string {
                if escaped {
                    escaped = false;
                } else if ch == b'\\' {
                    escaped = true;
                } else if ch == b'"' {
                    in_string = false;
                }
                continue;
            }
            if ch == b'"' {
                in_string = true;
            } else if ch == b'{' || ch == b'[' {
                depth += 1;
                if depth > JQ_MAX_DEPTH {
                    over_depth = true;
                    break;
                }
            } else if (ch == b'}' || ch == b']') && depth > 0 {
                depth -= 1;
            }
        }
        std::process::exit(refuse(if over_depth { "depth" } else { "parse" }));
    }

    let mut parser = Parser::new(text);
    let mut doc = match parser.parse_value(0) {
        Ok(v) => v,
        Err(_) => std::process::exit(refuse("parse")),
    };
    parser.ws();
    if parser.pos != parser.bytes.len() {
        std::process::exit(refuse("parse"));
    }

    let mut cur = &doc;
    for seg in &segments {
        cur = match cur {
            Json::Object(_) => match cur.member(seg) {
                Some(v) => v,
                None => std::process::exit(refuse("missing-key")),
            },
            Json::Array(items) => {
                let index = match parse_index(seg) {
                    Some(i) => i,
                    None => std::process::exit(refuse("bad-index")),
                };
                if index >= items.len() {
                    std::process::exit(refuse("bad-index"));
                }
                &items[index]
            }
            _ => std::process::exit(refuse("bad-path")),
        };
    }

    let rc = emit(cur, path);
    // Silence the unused-mut warning while keeping `doc` owned for the borrow.
    let _ = &mut doc;
    std::process::exit(rc);
}

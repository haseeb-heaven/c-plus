// Safe-Rust model of examples/utilities/data_tools/jsonfmt.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 jsonfmt.rs -o jsonfmt
const MAX_INPUT: usize = 2048;
const DEPTH_CAP: usize = 8;
const LINE_CAP: usize = 256;

fn refuse(code: &str, rc: i32) -> ! {
    println!("JSON error {}", code);
    std::process::exit(rc);
}

struct Parser<'a> {
    b: &'a [u8],
    i: usize,
    depth: usize,
    too_deep: bool,
}

impl<'a> Parser<'a> {
    fn ws(&mut self) {
        while self.i < self.b.len() && matches!(self.b[self.i], b' ' | b'\t' | b'\n' | b'\r') {
            self.i += 1;
        }
    }

    fn string(&mut self, out: &mut String) -> bool {
        if self.i >= self.b.len() || self.b[self.i] != b'"' {
            return false;
        }
        out.push('"');
        self.i += 1;
        while self.i < self.b.len() {
            let c = self.b[self.i];
            self.i += 1;
            match c {
                b'"' => {
                    out.push('"');
                    return true;
                }
                b'\\' => {
                    if self.i >= self.b.len() {
                        return false;
                    }
                    let e = self.b[self.i];
                    self.i += 1;
                    match e {
                        b'"' => out.push_str("\\\""),
                        b'\\' => out.push_str("\\\\"),
                        b'/' => out.push('/'),
                        b'b' => out.push_str("\\b"),
                        b'f' => out.push_str("\\f"),
                        b'n' => out.push_str("\\n"),
                        b'r' => out.push_str("\\r"),
                        b't' => out.push_str("\\t"),
                        b'u' => {
                            if self.i + 4 > self.b.len() {
                                return false;
                            }
                            let hex = &self.b[self.i..self.i + 4];
                            let s = std::str::from_utf8(hex).unwrap_or("");
                            let cp = match u32::from_str_radix(s, 16) {
                                Ok(v) => v,
                                Err(_) => return false,
                            };
                            self.i += 4;
                            match char::from_u32(cp) {
                                Some(ch) => out.push(ch),
                                None => return false,
                            }
                        }
                        _ => return false,
                    }
                }
                0..=0x1f => return false,
                _ => out.push(c as char),
            }
        }
        false
    }

    fn value(&mut self, out: &mut String) -> bool {
        self.ws();
        if self.i >= self.b.len() {
            return false;
        }
        match self.b[self.i] {
            b'"' => self.string(out),
            b'{' | b'[' => {
                let open = self.b[self.i];
                let close = if open == b'{' { b'}' } else { b']' };
                self.depth += 1;
                if self.depth > DEPTH_CAP {
                    self.too_deep = true;
                }
                out.push(open as char);
                self.i += 1;
                self.ws();
                if self.i < self.b.len() && self.b[self.i] == close {
                    self.i += 1;
                    out.push(close as char);
                    self.depth -= 1;
                    return true;
                }
                loop {
                    self.ws();
                    if open == b'{' {
                        if !self.string(out) {
                            return false;
                        }
                        self.ws();
                        if self.i >= self.b.len() || self.b[self.i] != b':' {
                            return false;
                        }
                        out.push(':');
                        self.i += 1;
                    }
                    if !self.value(out) {
                        return false;
                    }
                    self.ws();
                    if self.i >= self.b.len() {
                        return false;
                    }
                    if self.b[self.i] == b',' {
                        out.push(',');
                        self.i += 1;
                        continue;
                    }
                    if self.b[self.i] == close {
                        out.push(close as char);
                        self.i += 1;
                        self.depth -= 1;
                        return true;
                    }
                    return false;
                }
            }
            b't' if self.b[self.i..].starts_with(b"true") => {
                out.push_str("true");
                self.i += 4;
                true
            }
            b'f' if self.b[self.i..].starts_with(b"false") => {
                out.push_str("false");
                self.i += 5;
                true
            }
            b'n' if self.b[self.i..].starts_with(b"null") => {
                out.push_str("null");
                self.i += 4;
                true
            }
            b'-' | b'0'..=b'9' => {
                let start = self.i;
                if self.b[self.i] == b'-' {
                    self.i += 1;
                }
                if self.i >= self.b.len() || !self.b[self.i].is_ascii_digit() {
                    return false;
                }
                if self.b[self.i] == b'0' {
                    self.i += 1;
                } else {
                    while self.i < self.b.len() && self.b[self.i].is_ascii_digit() {
                        self.i += 1;
                    }
                }
                if self.i < self.b.len() && self.b[self.i] == b'.' {
                    self.i += 1;
                    if self.i >= self.b.len() || !self.b[self.i].is_ascii_digit() {
                        return false;
                    }
                    while self.i < self.b.len() && self.b[self.i].is_ascii_digit() {
                        self.i += 1;
                    }
                }
                if self.i < self.b.len() && matches!(self.b[self.i], b'e' | b'E') {
                    self.i += 1;
                    if self.i < self.b.len() && matches!(self.b[self.i], b'+' | b'-') {
                        self.i += 1;
                    }
                    if self.i >= self.b.len() || !self.b[self.i].is_ascii_digit() {
                        return false;
                    }
                    while self.i < self.b.len() && self.b[self.i].is_ascii_digit() {
                        self.i += 1;
                    }
                }
                out.push_str(std::str::from_utf8(&self.b[start..self.i]).unwrap_or(""));
                true
            }
            _ => false,
        }
    }
}

struct Pretty {
    line: Vec<u8>,
    depth: usize,
    need_indent: bool,
}

impl Pretty {
    fn new() -> Self {
        Self { line: Vec::new(), depth: 0, need_indent: true }
    }
    fn put(&mut self, ch: u8) -> bool {
        if self.need_indent {
            for _ in 0..self.depth {
                if self.line.len() + 1 >= LINE_CAP {
                    return false;
                }
                self.line.push(b' ');
            }
            self.need_indent = false;
        }
        if self.line.len() + 1 >= LINE_CAP {
            return false;
        }
        self.line.push(ch);
        true
    }
    fn flush(&mut self) {
        if !self.line.is_empty() {
            println!("JSON {}", String::from_utf8_lossy(&self.line));
        }
        self.need_indent = true;
    }
}

fn pretty(text: &str) -> bool {
    let mut p = Pretty::new();
    let mut in_string = false;
    let mut escaped = false;
    for &ch in text.as_bytes() {
        if in_string {
            if !p.put(ch) {
                return false;
            }
            if escaped {
                escaped = false;
            } else if ch == b'\\' {
                escaped = true;
            } else if ch == b'"' {
                in_string = false;
            }
            continue;
        }
        match ch {
            b'"' => {
                in_string = true;
                if !p.put(ch) {
                    return false;
                }
            }
            b'{' | b'[' => {
                if !p.put(ch) {
                    return false;
                }
                p.flush();
                p.line.clear();
                p.depth += 1;
            }
            b'}' | b']' => {
                p.flush();
                p.line.clear();
                if p.depth > 0 {
                    p.depth -= 1;
                }
                if !p.put(ch) {
                    return false;
                }
            }
            b',' => {
                if !p.put(ch) {
                    return false;
                }
                p.flush();
                p.line.clear();
            }
            b':' => {
                if !p.put(ch) || !p.put(b' ') {
                    return false;
                }
            }
            b' ' | b'\t' | b'\n' | b'\r' => {}
            _ => {
                if !p.put(ch) {
                    return false;
                }
            }
        }
    }
    p.flush();
    true
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        refuse("usage", 2);
    }
    let text = args[1].as_bytes();
    if text.is_empty() {
        refuse("empty", 1);
    }
    if text.len() > MAX_INPUT {
        refuse("too-long", 1);
    }
    let root_kind = {
        let mut i = 0;
        while i < text.len() && matches!(text[i], b' ' | b'\t' | b'\n' | b'\r') {
            i += 1;
        }
        if i < text.len() {
            text[i]
        } else {
            refuse("parse", 1);
        }
    };
    let mut parser = Parser {
        b: text,
        i: 0,
        depth: 0,
        too_deep: false,
    };
    let mut canonical = String::new();
    if !parser.value(&mut canonical) {
        refuse(if parser.too_deep { "depth" } else { "parse" }, 1);
    }
    parser.ws();
    if parser.i != text.len() {
        refuse("parse", 1);
    }
    if canonical.len() + 1 > 2048 {
        refuse("too-long", 1);
    }
    match root_kind {
        b'{' => println!("JSON root object"),
        b'[' => println!("JSON root array"),
        _ => println!("JSON root scalar"),
    }
    if !pretty(&canonical) {
        refuse("line-too-long", 1);
    }
}

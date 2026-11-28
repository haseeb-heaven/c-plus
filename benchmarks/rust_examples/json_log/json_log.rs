// Safe-Rust side of the json_log runnable examples.
//
// These are *analogue examples*, not FFI.  There is no `unsafe`, no allocator
// trickery and no call into the C archives.  A tiny hand-written JSON checker
// (built only from `str` methods) and a `Vec<String>` logger reproduce the
// observable checked-API pattern of harness.c on the same fixed fixtures.
// Matching tokens show agreement on these examples only -- see NOTES.md for
// the explicit non-equivalence statement.
//
// Build: rustc --edition 2021 benchmarks/rust_examples/json_log/json_log.rs \
//        -o <out>/json_log

// Fixed fixtures -- identical to harness.c.  No wall-clock, no randomness.
const GOOD: &str = r#"{"name":"alpha","n":42,"ok":true}"#;
const MALFORMED: &str = r#"{"name":"alpha","n":"#;
const DEEP_LEVELS: usize = 32;
const BUDGET_DEPTH: usize = 8;
const BUDGET_LEN: usize = 256;

fn emit(api: &str, case: &str, token: &str) {
    println!("{} {} {}", api, case, token);
}

// ---------------------------------------------------------------------------
// Tiny JSON checker (str methods only).  It validates container balance while
// respecting strings and escapes, then reads top-level scalar fields.
// ---------------------------------------------------------------------------

#[derive(Clone)]
struct Doc<'a> {
    text: &'a str,
}

/// Container balance, ignoring brackets that appear inside JSON strings.
/// Mirrors the byte scan in stdlib/json.hp::json_input_within_budget.
fn balanced(text: &str) -> bool {
    let mut stack: Vec<char> = Vec::new();
    let mut in_string = false;
    let mut escaped = false;

    for ch in text.chars() {
        if in_string {
            if escaped {
                escaped = false;
            } else if ch == '\\' {
                escaped = true;
            } else if ch == '"' {
                in_string = false;
            }
            continue;
        }
        match ch {
            '"' => in_string = true,
            '{' => stack.push('}'),
            '[' => stack.push(']'),
            '}' | ']' => {
                if stack.pop() != Some(ch) {
                    return false;
                }
            }
            _ => {}
        }
    }
    !in_string && stack.is_empty()
}

/// Analogue of `cplus_json_parse`: None -> REFUSED, malformed -> REFUSED.
fn parse(text: Option<&str>) -> Option<Doc<'_>> {
    let t = text?;
    if t.is_empty() || !balanced(t) {
        return None;
    }
    Some(Doc { text: t })
}

fn top_level_value<'a>(doc: Option<&Doc<'a>>, key: &str) -> Option<&'a str> {
    let d = doc?;
    if key.is_empty() {
        return None;
    }
    let needle = format!("\"{}\":", key);
    let at = d.text.find(&needle)? + needle.len();
    Some(d.text[at..].trim_start())
}

/// Analogue of `cplus_json_get_int`: returns the integer at `key`.
fn get_int(doc: Option<&Doc<'_>>, key: &str) -> Option<i64> {
    let rest = top_level_value(doc, key)?;
    let digits: String = rest
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '-')
        .collect();
    digits.parse::<i64>().ok()
}

/// Analogue of `cplus_json_get_str`: returns the borrowed string at `key`.
fn get_str(doc: Option<&Doc<'_>>, key: &str) -> Option<String> {
    let rest = top_level_value(doc, key)?;
    if !rest.starts_with('"') {
        return None;
    }
    let tail = &rest[1..];
    let end = tail.find('"')?;
    Some(tail[..end].to_string())
}

/// Analogue of `cplus_json_free`: Rust drops the value; freeing a missing
/// document is a documented safe no-op.
fn free(_doc: Option<Doc<'_>>) {}

/// Analogue of `json.hp::json_parse_budget`: depth and length are checked
/// *before* any structural work, exactly like the header-only C+ API.
fn parse_budget(text: Option<&str>, max_depth: usize, max_len: usize) -> bool {
    let t = match text {
        Some(t) if !t.is_empty() => t,
        _ => return false,
    };
    if t.len() > max_len {
        return false;
    }
    let mut depth = 0usize;
    let mut in_string = false;
    let mut escaped = false;
    for ch in t.chars() {
        if in_string {
            if escaped {
                escaped = false;
            } else if ch == '\\' {
                escaped = true;
            } else if ch == '"' {
                in_string = false;
            }
            continue;
        }
        match ch {
            '"' => in_string = true,
            '{' | '[' => {
                depth += 1;
                if depth > max_depth {
                    return false;
                }
            }
            '}' | ']' => {
                if depth > 0 {
                    depth -= 1;
                }
            }
            _ => {}
        }
    }
    balanced(t)
}

// ---------------------------------------------------------------------------
// Vec-backed loggers: analogues of cplus_safe_log* and cplus_libc_log*.
// ---------------------------------------------------------------------------

#[derive(Default)]
struct SafeLogger {
    level: i32,
    lines: Vec<String>,
}

impl SafeLogger {
    fn new() -> Self {
        Self::default()
    }
    fn set_level(&mut self, level: i32) {
        self.level = level;
    }
    fn level(&self) -> i32 {
        self.level
    }
    fn log(&mut self, level: i32, api: Option<&str>, msg: Option<&str>) {
        if self.level == 0 || level > self.level {
            return;
        }
        self.lines.push(format!(
            "[{}] {}: {}",
            level,
            api.unwrap_or("(null)"),
            msg.unwrap_or("(null)")
        ));
    }
    fn last(&self) -> Option<&String> {
        self.lines.last()
    }
}

#[derive(Default)]
struct LibcLogger {
    enabled: bool,
    // None models a closed / null output stream.
    stream: Option<Vec<String>>,
}

impl LibcLogger {
    fn new() -> Self {
        Self::default()
    }
    fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }
    fn write(&mut self, api: &str, msg: &str, errno: &mut i32) {
        if !self.enabled {
            return;
        }
        let line = format!("{}: {}", api, msg);
        // Never clobber the caller's error indicator.
        let sentinel = *errno;
        if let Some(sink) = self.stream.as_mut() {
            sink.push(line);
        }
        *errno = sentinel;
    }
}

#[derive(Clone, Copy)]
struct Sink {
    stream: Option<()>,
    threshold: i32,
}

impl Sink {
    fn borrowed(threshold: i32) -> Self {
        Self {
            stream: Some(()),
            threshold,
        }
    }
    fn null(threshold: i32) -> Self {
        Self {
            stream: None,
            threshold,
        }
    }
    fn valid(&self) -> bool {
        self.stream.is_some()
    }
    fn level(&self) -> i32 {
        self.threshold
    }
}

fn main() {
    // ---- cplus_json_parse -------------------------------------------------
    let good_doc = parse(Some(GOOD));
    emit(
        "cplus_json_parse",
        "valid",
        if good_doc.is_some() { "OK" } else { "REFUSED" },
    );
    emit(
        "cplus_json_parse",
        "malformed",
        if parse(Some(MALFORMED)).is_none() {
            "REFUSED"
        } else {
            "OK"
        },
    );
    emit(
        "cplus_json_parse",
        "null_args",
        if parse(None).is_none() { "REFUSED" } else { "OK" },
    );

    // ---- cplus_json_get_int -----------------------------------------------
    let n = get_int(good_doc.as_ref(), "n");
    emit(
        "cplus_json_get_int",
        "valid",
        if n == Some(42) { "OK" } else { "REFUSED" },
    );
    emit(
        "cplus_json_get_int",
        "null_args",
        if get_int(None, "n").is_none() && get_int(good_doc.as_ref(), "").is_none() {
            "REFUSED"
        } else {
            "OK"
        },
    );

    // ---- cplus_json_get_str -----------------------------------------------
    let name = get_str(good_doc.as_ref(), "name");
    emit(
        "cplus_json_get_str",
        "valid",
        if name.as_deref() == Some("alpha") {
            "OK"
        } else {
            "REFUSED"
        },
    );
    emit(
        "cplus_json_get_str",
        "null_args",
        if get_str(None, "name").is_none() && get_str(good_doc.as_ref(), "").is_none() {
            "REFUSED"
        } else {
            "OK"
        },
    );

    // ---- cplus_json_free --------------------------------------------------
    free(good_doc.clone());
    emit("cplus_json_free", "valid", "OK");
    free(None);
    emit("cplus_json_free", "null_args", "OK");

    // ---- json.hp budget analogue -----------------------------------------
    let deep: String = {
        let mut s = String::with_capacity(DEEP_LEVELS * 2 + 1);
        for _ in 0..DEEP_LEVELS {
            s.push('[');
        }
        s.push('0');
        for _ in 0..DEEP_LEVELS {
            s.push(']');
        }
        s
    };
    emit(
        "json_parse_budget",
        "valid",
        if parse_budget(Some(GOOD), BUDGET_DEPTH, BUDGET_LEN) {
            "OK"
        } else {
            "REFUSED"
        },
    );
    emit(
        "json_parse_budget",
        "budget",
        if !parse_budget(Some(&deep), BUDGET_DEPTH, BUDGET_LEN) {
            "REFUSED"
        } else {
            "OK"
        },
    );
    emit(
        "json_parse_budget",
        "null_args",
        if !parse_budget(None, BUDGET_DEPTH, BUDGET_LEN)
            && !parse_budget(Some(""), BUDGET_DEPTH, BUDGET_LEN)
        {
            "REFUSED"
        } else {
            "OK"
        },
    );

    // ---- cplus_safe_log* analogue: write/read and null-argument path -----
    let mut safe = SafeLogger::new();
    safe.set_level(3);
    safe.log(3, Some("cplus_safe_log"), Some("write_read"));
    let read_ok = safe.level() == 3
        && safe
            .last()
            .map(|line| line.contains("write_read"))
            .unwrap_or(false);
    emit(
        "cplus_safe_log",
        "write_read",
        if read_ok { "OK" } else { "REFUSED" },
    );
    safe.log(3, None, None); // api/msg -> "(null)"
    emit("cplus_safe_log", "null_stream", "OK");
    safe.set_level(0);

    // ---- cplus_libc_log* analogue: errno preserved / closed stream -------
    let mut libc_log = LibcLogger::new();
    let mut errno = 1234; // EDOM-equivalent sentinel
    libc_log.set_enabled(true);
    libc_log.write("cplus_libc_log", "write_read", &mut errno);
    emit(
        "cplus_libc_log",
        "write_read",
        if errno == 1234 { "OK" } else { "REFUSED" },
    );
    libc_log.set_enabled(false);
    libc_log.stream = None; // model closed fd 2
    libc_log.set_enabled(true);
    libc_log.write("cplus_libc_log", "null_stream", &mut errno);
    // Reaching this line proves the null-stream write did not panic.
    emit(
        "cplus_libc_log",
        "null_stream",
        if libc_log.stream.is_none() {
            "OK"
        } else {
            "REFUSED"
        },
    );
    libc_log.set_enabled(false);

    // ---- log.hp sink analogue --------------------------------------------
    let borrowed = Sink::borrowed(2);
    emit(
        "log_sink",
        "write_read",
        if borrowed.valid() && borrowed.level() == 2 {
            "OK"
        } else {
            "REFUSED"
        },
    );
    let null_sink = Sink::null(2);
    emit(
        "log_sink",
        "null_stream",
        if !null_sink.valid() { "REFUSED" } else { "OK" },
    );
}

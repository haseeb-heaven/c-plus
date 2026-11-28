// Safe-Rust model of examples/utilities/net_tools/httpline.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 httpline.rs -o httpline
const OK: i32 = 0;
const BOUNDS: i32 = 101;
const UNSUPPORTED: i32 = 108;

fn category(code: i32) -> &'static str {
    match code {
        BOUNDS => "bounds",
        UNSUPPORTED => "invalid",
        _ => "invalid",
    }
}

fn known_method(m: &str) -> bool {
    matches!(m, "GET" | "POST" | "PUT" | "DELETE" | "HEAD" | "OPTIONS" | "PATCH")
}

fn validate(text: &str) -> (i32, String, String, String, &'static str) {
    let blank = (String::new(), String::new(), String::new());
    if text.is_empty() {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "empty");
    }
    if text.len() > 128 {
        return (BOUNDS, blank.0, blank.1, blank.2, "too-long");
    }
    let fields: Vec<&str> = text.split(' ').collect();
    if fields.len() != 3 {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "wrong-field-count");
    }
    let (m, p, v) = (fields[0], fields[1], fields[2]);
    if m.len() >= 16 || p.len() >= 96 || v.len() >= 16 {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "field-extract");
    }
    if m.is_empty() || p.is_empty() || v.is_empty() {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "empty-field");
    }
    if !known_method(m) {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "bad-method");
    }
    if v != "HTTP/1.0" && v != "HTTP/1.1" {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "bad-version");
    }
    if p.len() > 80 {
        return (BOUNDS, blank.0, blank.1, blank.2, "path-too-long");
    }
    if !p.starts_with('/') {
        return (UNSUPPORTED, blank.0, blank.1, blank.2, "path-not-absolute");
    }
    let pb = p.as_bytes();
    for (i, &c) in pb.iter().enumerate() {
        if !(0x21..=0x7e).contains(&c) {
            return (UNSUPPORTED, blank.0, blank.1, blank.2, "bad-path-char");
        }
        if c == b'.' && i > 0 && pb[i - 1] == b'.' {
            return (UNSUPPORTED, blank.0, blank.1, blank.2, "dot-dot-path");
        }
    }
    (OK, m.to_string(), p.to_string(), v.to_string(), "ok")
}

fn case(name: &str, value: &str, expect: i32, counts: &mut [i32; 4]) {
    let (code, m, p, v, reason) = validate(value);
    counts[0] += 1;
    if code == OK {
        if expect != OK {
            println!("HTTP {} FAIL expected-refusal {}", name, expect);
            counts[3] += 1;
        } else {
            println!("HTTP {} VALID {} {} {}", name, m, p, v);
            counts[1] += 1;
        }
        return;
    }
    if expect != 0 && expect != code {
        println!("HTTP {} FAIL wrong-code {} {} {}", name, code, expect, reason);
        counts[3] += 1;
    } else {
        println!("HTTP {} REFUSED {} {} {}", name, code, category(code), reason);
        counts[2] += 1;
    }
}

fn main() {
    let oversized = format!("GET /{} HTTP/1.1", "x".repeat(120));
    let builtin: [(&str, &str, i32); 13] = [
        ("v_get", "GET /index.html HTTP/1.1", OK),
        ("v_post", "POST /submit HTTP/1.0", OK),
        ("v_head", "HEAD / HTTP/1.1", OK),
        ("h_bad_method", "BREW /x HTTP/1.1", UNSUPPORTED),
        ("h_bad_ver", "GET /x HTTP/2.0", UNSUPPORTED),
        ("h_two_fields", "GET /x", UNSUPPORTED),
        ("h_four_fields", "GET /x HTTP/1.1 extra", UNSUPPORTED),
        ("h_double_space", "GET  /x HTTP/1.1", UNSUPPORTED),
        ("h_missing_path", "GET  HTTP/1.1", UNSUPPORTED),
        ("h_relative", "GET x HTTP/1.1", UNSUPPORTED),
        ("h_traversal", "GET /../etc/passwd HTTP/1.1", UNSUPPORTED),
        ("h_trailing_space", "GET /x HTTP/1.1 ", UNSUPPORTED),
        ("h_oversized", "", BOUNDS),
    ];
    let mut counts = [0i32; 4];
    for (i, (name, value, expect)) in builtin.iter().enumerate() {
        let v = if i == 12 { oversized.as_str() } else { value };
        case(name, v, *expect, &mut counts);
    }
    for v in std::env::args().skip(1) {
        case("argv", &v, OK, &mut counts);
    }
    println!(
        "HTTP_SUMMARY {} {} {} {}",
        counts[0], counts[1], counts[2], counts[3]
    );
    if counts[3] == 0 {
        std::process::exit(0);
    }
    std::process::exit(1);
}

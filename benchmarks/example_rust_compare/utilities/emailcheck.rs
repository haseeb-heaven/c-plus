// Safe-Rust model of examples/utilities/net_tools/emailcheck.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 emailcheck.rs -o emailcheck
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

fn alnum(c: u8) -> bool {
    c.is_ascii_alphanumeric()
}
fn alpha(c: u8) -> bool {
    c.is_ascii_alphabetic()
}

fn validate_local(l: &[u8]) -> (i32, &'static str) {
    if l.is_empty() {
        return (UNSUPPORTED, "empty-local");
    }
    if l.len() > 32 {
        return (BOUNDS, "local-too-long");
    }
    if l[0] == b'.' || l[l.len() - 1] == b'.' {
        return (UNSUPPORTED, "bad-local-edge");
    }
    for (i, &c) in l.iter().enumerate() {
        if !(alnum(c) || matches!(c, b'.' | b'_' | b'%' | b'+' | b'-')) {
            return (UNSUPPORTED, "bad-local-char");
        }
        if c == b'.' && i > 0 && l[i - 1] == b'.' {
            return (UNSUPPORTED, "consecutive-dot");
        }
    }
    (OK, "ok")
}

fn validate_domain(d: &[u8]) -> (i32, &'static str) {
    if d.is_empty() {
        return (UNSUPPORTED, "empty-domain");
    }
    if d[0] == b'.' || d[d.len() - 1] == b'.' {
        return (UNSUPPORTED, "bad-domain-edge");
    }
    let mut labels: Vec<&[u8]> = d.split(|&c| c == b'.').collect();
    for label in &mut labels {
        if label.is_empty() {
            return (UNSUPPORTED, "empty-label");
        }
        if label.len() > 63 {
            return (BOUNDS, "label-too-long");
        }
        if label[0] == b'-' || label[label.len() - 1] == b'-' {
            return (UNSUPPORTED, "bad-label-edge");
        }
        if !label.iter().all(|&c| alnum(c) || c == b'-') {
            return (UNSUPPORTED, "bad-domain-char");
        }
    }
    if labels.len() < 2 {
        return (UNSUPPORTED, "no-dot");
    }
    let tld = labels[labels.len() - 1];
    if tld.len() < 2 {
        return (UNSUPPORTED, "short-tld");
    }
    if !tld.iter().all(|&c| alpha(c)) {
        return (UNSUPPORTED, "bad-tld");
    }
    (OK, "ok")
}

fn validate(text: &str) -> (i32, String, String, &'static str) {
    let b = text.as_bytes();
    if b.is_empty() {
        return (UNSUPPORTED, String::new(), String::new(), "empty");
    }
    if b.len() > 96 {
        return (BOUNDS, String::new(), String::new(), "too-long");
    }
    let at = match b.iter().position(|&c| c == b'@') {
        Some(p) => p,
        None => return (UNSUPPORTED, String::new(), String::new(), "no-at"),
    };
    if at == 0 {
        return (UNSUPPORTED, String::new(), String::new(), "empty-local");
    }
    if at == b.len() - 1 {
        return (UNSUPPORTED, String::new(), String::new(), "empty-domain");
    }
    let local = &text[..at];
    let domain = &text[at + 1..];
    if domain.as_bytes().contains(&b'@') {
        return (UNSUPPORTED, String::new(), String::new(), "multiple-at");
    }
    let (code, reason) = validate_local(local.as_bytes());
    if code != OK {
        return (code, String::new(), String::new(), reason);
    }
    let (code, reason) = validate_domain(domain.as_bytes());
    if code != OK {
        return (code, String::new(), String::new(), reason);
    }
    (OK, local.to_string(), domain.to_string(), "ok")
}

fn case(name: &str, value: &str, expect: i32, counts: &mut [i32; 4]) {
    let (code, local, domain, reason) = validate(value);
    counts[0] += 1;
    if code == OK {
        if expect != OK {
            println!("EM {} FAIL expected-refusal {}", name, expect);
            counts[3] += 1;
        } else {
            println!("EM {} VALID {} {}", name, local, domain);
            counts[1] += 1;
        }
        return;
    }
    if expect != 0 && expect != code {
        println!("EM {} FAIL wrong-code {} {} {}", name, code, expect, reason);
        counts[3] += 1;
    } else {
        println!("EM {} REFUSED {} {} {}", name, code, category(code), reason);
        counts[2] += 1;
    }
}

fn main() {
    let long_local = format!("{}@example.com", "a".repeat(34));
    let long_total = "a".repeat(100);
    let builtin: [(&str, &str, i32); 14] = [
        ("v_simple", "alice@example.com", OK),
        ("v_plus_tag", "a.b+tag@sub.example.co.uk", OK),
        ("v_subdomain", "x@y.io", OK),
        ("h_no_at", "no-at-sign", UNSUPPORTED),
        ("h_double_at", "a@@b.com", UNSUPPORTED),
        ("h_empty_local", "@example.com", UNSUPPORTED),
        ("h_empty_dom", "alice@", UNSUPPORTED),
        ("h_dotdot", "a..b@example.com", UNSUPPORTED),
        ("h_leading_dot", ".alice@example.com", UNSUPPORTED),
        ("h_no_dot", "alice@example", UNSUPPORTED),
        ("h_short_tld", "alice@example.c", UNSUPPORTED),
        ("h_bad_label", "alice@-example.com", UNSUPPORTED),
        ("h_long_local", "", BOUNDS),
        ("h_long_total", "", BOUNDS),
    ];
    let mut counts = [0i32; 4];
    for (i, (name, value, expect)) in builtin.iter().enumerate() {
        let v = match i {
            12 => long_local.as_str(),
            13 => long_total.as_str(),
            _ => value,
        };
        case(name, v, *expect, &mut counts);
    }
    for v in std::env::args().skip(1) {
        case("argv", &v, OK, &mut counts);
    }
    println!(
        "EM_SUMMARY {} {} {} {}",
        counts[0], counts[1], counts[2], counts[3]
    );
    if counts[3] == 0 {
        std::process::exit(0);
    }
    std::process::exit(1);
}

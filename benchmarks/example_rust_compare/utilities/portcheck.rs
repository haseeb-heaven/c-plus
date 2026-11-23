// Safe-Rust model of examples/utilities/net_tools/portcheck.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 portcheck.rs -o portcheck
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

fn validate(text: &str) -> (i32, u32, &'static str) {
    let b = text.as_bytes();
    if b.is_empty() {
        return (UNSUPPORTED, 0, "empty");
    }
    if b.len() > 5 {
        return (BOUNDS, 0, "too-long");
    }
    let mut v: u32 = 0;
    for &c in b {
        if !c.is_ascii_digit() {
            return (UNSUPPORTED, 0, "non-digit");
        }
        let d = (c - b'0') as u32;
        if v > 6553 {
            return (BOUNDS, 0, "overflow");
        }
        v = v * 10 + d;
        if v > 65535 {
            return (BOUNDS, 0, "overflow");
        }
    }
    if b.len() > 1 && b[0] == b'0' {
        return (UNSUPPORTED, 0, "leading-zero");
    }
    (OK, v, "ok")
}

fn case(name: &str, value: &str, expect: i32, counts: &mut [i32; 4]) {
    let (code, port, reason) = validate(value);
    counts[0] += 1;
    if code == OK {
        if expect != OK {
            println!("PORT {} FAIL expected-refusal {}", name, expect);
            counts[3] += 1;
        } else {
            println!("PORT {} VALID {}", name, port);
            counts[1] += 1;
        }
        return;
    }
    if expect != 0 && expect != code {
        println!("PORT {} FAIL wrong-code {} {} {}", name, code, expect, reason);
        counts[3] += 1;
    } else {
        println!("PORT {} REFUSED {} {} {}", name, code, category(code), reason);
        counts[2] += 1;
    }
}

fn main() {
    let builtin: [(&str, &str, i32); 12] = [
        ("v_min", "0", OK),
        ("v_max", "65535", OK),
        ("v_https", "443", OK),
        ("h_negative", "-1", UNSUPPORTED),
        ("h_too_big", "65536", BOUNDS),
        ("h_huge", "99999999999999999999", BOUNDS),
        ("h_not_num", "abc", UNSUPPORTED),
        ("h_trailing", "80x", UNSUPPORTED),
        ("h_empty", "", UNSUPPORTED),
        ("h_plus", "+80", UNSUPPORTED),
        ("h_space", " 80", UNSUPPORTED),
        ("h_leading_zero", "00080", UNSUPPORTED),
    ];
    let mut counts = [0i32; 4];
    for (name, value, expect) in builtin {
        case(name, value, expect, &mut counts);
    }
    for v in std::env::args().skip(1) {
        case("argv", &v, OK, &mut counts);
    }
    println!(
        "PORT_SUMMARY {} {} {} {}",
        counts[0], counts[1], counts[2], counts[3]
    );
    if counts[3] == 0 {
        std::process::exit(0);
    }
    std::process::exit(1);
}

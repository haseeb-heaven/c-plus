// Safe-Rust model of examples/utilities/net_tools/ipcheck.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 ipcheck.rs -o ipcheck

const OK: i32 = 0;
const BOUNDS: i32 = 101;
const UNSUPPORTED: i32 = 108;

fn category(code: i32) -> &'static str {
    match code {
        BOUNDS => "bounds",
        UNSUPPORTED => "invalid",
        107 => "memory",
        109 => "io",
        _ => "invalid",
    }
}

fn validate(text: &str, out: &mut [u32; 4]) -> (i32, &'static str) {
    for x in out.iter_mut() {
        *x = 0;
    }
    let b = text.as_bytes();
    if b.is_empty() {
        return (UNSUPPORTED, "empty");
    }
    if b.len() > 15 {
        return (BOUNDS, "too-long");
    }
    let fields: Vec<&[u8]> = b.split(|&c| c == b'.').collect();
    if fields.len() != 4 {
        return (UNSUPPORTED, "wrong-field-count");
    }
    for (f, field) in fields.iter().enumerate() {
        if field.len() >= 8 {
            return (UNSUPPORTED, "field-extract");
        }
        if field.is_empty() {
            return (UNSUPPORTED, "empty-octet");
        }
        if field.len() > 3 {
            return (BOUNDS, "octet-too-long");
        }
        if field.len() > 1 && field[0] == b'0' {
            return (UNSUPPORTED, "leading-zero");
        }
        let mut v: u32 = 0;
        for &c in *field {
            if !c.is_ascii_digit() {
                return (UNSUPPORTED, "non-digit");
            }
            if v > 25 {
                return (BOUNDS, "octet-overflow");
            }
            v = v * 10 + (c - b'0') as u32;
            if v > 255 {
                return (BOUNDS, "octet-overflow");
            }
        }
        out[f] = v;
    }
    (OK, "ok")
}

fn case(name: &str, value: &str, expect: i32, counts: &mut [i32; 4]) {
    let mut o = [0u32; 4];
    let (code, reason) = validate(value, &mut o);
    counts[0] += 1;
    if code == OK {
        if expect != OK {
            println!("IP {} FAIL expected-refusal {}", name, expect);
            counts[3] += 1;
        } else {
            println!("IP {} VALID {} {} {} {}", name, o[0], o[1], o[2], o[3]);
            counts[1] += 1;
        }
        return;
    }
    if expect != 0 && expect != code {
        println!("IP {} FAIL wrong-code {} {} {}", name, code, expect, reason);
        counts[3] += 1;
    } else {
        println!("IP {} REFUSED {} {} {}", name, code, category(code), reason);
        counts[2] += 1;
    }
}

fn main() {
    let builtin: [(&str, &str, i32); 13] = [
        ("v_max", "255.255.255.255", OK),
        ("v_zero", "0.0.0.0", OK),
        ("v_loopback", "127.0.0.1", OK),
        ("h_256", "256.0.0.1", BOUNDS),
        ("h_3fields", "1.2.3", UNSUPPORTED),
        ("h_5fields", "1.2.3.4.5", UNSUPPORTED),
        ("h_empty_oct", "1..3.4", UNSUPPORTED),
        ("h_leading_zero", "01.2.3.4", UNSUPPORTED),
        ("h_999", "1.2.3.999", BOUNDS),
        ("h_long", "123456789012.1.1.1", BOUNDS),
        ("h_alpha", "abc.1.1.1", UNSUPPORTED),
        ("h_negative", "1.2.3.-4", UNSUPPORTED),
        ("h_empty", "", UNSUPPORTED),
    ];
    let mut counts = [0i32; 4];
    for (name, value, expect) in builtin {
        case(name, value, expect, &mut counts);
    }
    for v in std::env::args().skip(1) {
        case("argv", &v, OK, &mut counts);
    }
    println!(
        "IP_SUMMARY {} {} {} {}",
        counts[0], counts[1], counts[2], counts[3]
    );
    if counts[3] == 0 {
        std::process::exit(0);
    }
    std::process::exit(1);
}

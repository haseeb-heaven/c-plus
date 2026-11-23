// Safe-Rust model of examples/utilities/net_tools/urlcheck.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 urlcheck.rs -o urlcheck
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

#[allow(clippy::too_many_arguments)]
fn validate(text: &str) -> (i32, String, String, u32, bool, String, &'static str) {
    let blank = || {
        (
            String::new(),
            String::new(),
            0u32,
            false,
            String::new(),
        )
    };
    let b = text.as_bytes();
    if b.is_empty() {
        let (s, h, p, e, pa) = blank();
        return (UNSUPPORTED, s, h, p, e, pa, "empty");
    }
    if b.len() > 128 {
        let (s, h, p, e, pa) = blank();
        return (BOUNDS, s, h, p, e, pa, "too-long");
    }
    let sep = match text.find("://") {
        Some(p) => p,
        None => {
            let (s, h, p, e, pa) = blank();
            return (UNSUPPORTED, s, h, p, e, pa, "no-scheme");
        }
    };
    let scheme_len = sep;
    if scheme_len == 0 {
        let (s, h, p, e, pa) = blank();
        return (UNSUPPORTED, s, h, p, e, pa, "empty-scheme");
    }
    if scheme_len > 8 {
        let (s, h, p, e, pa) = blank();
        return (BOUNDS, s, h, p, e, pa, "scheme-too-long");
    }
    let scheme = &text[..scheme_len];
    if scheme != "http" && scheme != "https" {
        let (s, h, p, e, pa) = blank();
        return (UNSUPPORTED, s, h, p, e, pa, "bad-scheme");
    }
    let rest = &text[scheme_len + 3..];
    let (auth, path) = match rest.find('/') {
        Some(slash) => {
            if rest.len() - slash > 64 {
                let (s, h, p, e, pa) = blank();
                return (BOUNDS, s, h, p, e, pa, "path-too-long");
            }
            (&rest[..slash], rest[slash..].to_string())
        }
        None => (rest, "/".to_string()),
    };
    let pb = path.as_bytes();
    for (i, &c) in pb.iter().enumerate() {
        if !(0x21..=0x7e).contains(&c) {
            let (s, h, p, e, pa) = blank();
            return (UNSUPPORTED, s, h, p, e, pa, "bad-path-char");
        }
        if c == b'.' && i > 0 && pb[i - 1] == b'.' {
            let (s, h, p, e, pa) = blank();
            return (UNSUPPORTED, s, h, p, e, pa, "dot-dot-path");
        }
    }
    if auth.is_empty() {
        let (s, h, p, e, pa) = blank();
        return (UNSUPPORTED, s, h, p, e, pa, "empty-authority");
    }
    let (host, port, explicit) = match auth.find(':') {
        Some(colon) => {
            let port_len = auth.len() - colon - 1;
            if port_len == 0 {
                let (s, h, p, e, pa) = blank();
                return (UNSUPPORTED, s, h, p, e, pa, "empty-port");
            }
            if port_len > 5 {
                let (s, h, p, e, pa) = blank();
                return (BOUNDS, s, h, p, e, pa, "port-too-long");
            }
            let portstr = &auth[colon + 1..];
            if portstr.contains(':') {
                let (s, h, p, e, pa) = blank();
                return (UNSUPPORTED, s, h, p, e, pa, "bad-authority");
            }
            let mut v: u32 = 0;
            for c in portstr.bytes() {
                if !c.is_ascii_digit() {
                    let (s, h, p, e, pa) = blank();
                    return (UNSUPPORTED, s, h, p, e, pa, "bad-port");
                }
                if v > 6553 {
                    let (s, h, p, e, pa) = blank();
                    return (BOUNDS, s, h, p, e, pa, "port-overflow");
                }
                v = v * 10 + (c - b'0') as u32;
                if v > 65535 {
                    let (s, h, p, e, pa) = blank();
                    return (BOUNDS, s, h, p, e, pa, "port-overflow");
                }
            }
            if v == 0 {
                let (s, h, p, e, pa) = blank();
                return (UNSUPPORTED, s, h, p, e, pa, "port-zero");
            }
            (&auth[..colon], v, true)
        }
        None => {
            let default = if scheme == "https" { 443 } else { 80 };
            (auth, default, false)
        }
    };
    if host.is_empty() {
        let (s, h, p, e, pa) = blank();
        return (UNSUPPORTED, s, h, p, e, pa, "empty-host");
    }
    if host.len() > 63 {
        let (s, h, p, e, pa) = blank();
        return (BOUNDS, s, h, p, e, pa, "host-too-long");
    }
    let hb = host.as_bytes();
    if hb[0] == b'.' || hb[0] == b'-' || hb[hb.len() - 1] == b'.' || hb[hb.len() - 1] == b'-' {
        let (s, h, p, e, pa) = blank();
        return (UNSUPPORTED, s, h, p, e, pa, "bad-host-edge");
    }
    for (i, &c) in hb.iter().enumerate() {
        if !(alnum(c) || c == b'.' || c == b'-') {
            let (s, h, p, e, pa) = blank();
            return (UNSUPPORTED, s, h, p, e, pa, "bad-host-char");
        }
        if c == b'.' && i > 0 && hb[i - 1] == b'.' {
            let (s, h, p, e, pa) = blank();
            return (UNSUPPORTED, s, h, p, e, pa, "consecutive-dot");
        }
    }
    (
        OK,
        scheme.to_string(),
        host.to_string(),
        port,
        explicit,
        path,
        "ok",
    )
}

fn case(name: &str, value: &str, expect: i32, counts: &mut [i32; 4]) {
    let (code, scheme, host, port, explicit, path, reason) = validate(value);
    counts[0] += 1;
    if code == OK {
        if expect != OK {
            println!("URL {} FAIL expected-refusal {}", name, expect);
            counts[3] += 1;
        } else {
            println!(
                "URL {} VALID {} {} {} {} {}",
                name,
                scheme,
                host,
                port,
                path,
                if explicit { "explicit" } else { "default" }
            );
            counts[1] += 1;
        }
        return;
    }
    if expect != 0 && expect != code {
        println!("URL {} FAIL wrong-code {} {} {}", name, code, expect, reason);
        counts[3] += 1;
    } else {
        println!("URL {} REFUSED {} {} {}", name, code, category(code), reason);
        counts[2] += 1;
    }
}

fn main() {
    let oversized = format!("http://{}", "a".repeat(130));
    let builtin: [(&str, &str, i32); 13] = [
        ("v_http", "http://example.com/", OK),
        ("v_https_port", "https://example.com:8443/a/b", OK),
        ("v_ip_port", "http://127.0.0.1:8080", OK),
        ("h_bad_scheme", "ftp://example.com/", UNSUPPORTED),
        ("h_empty_auth", "http://", UNSUPPORTED),
        ("h_no_scheme", "not a url", UNSUPPORTED),
        ("h_space_host", "http://exa mple.com/", UNSUPPORTED),
        ("h_big_port", "http://example.com:99999/", BOUNDS),
        ("h_alpha_port", "http://example.com:abc/", UNSUPPORTED),
        ("h_empty_port", "http://example.com:/", UNSUPPORTED),
        ("h_bad_edge", "http://.example.com/", UNSUPPORTED),
        ("h_traversal", "http://example.com/../etc", UNSUPPORTED),
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
        "URL_SUMMARY {} {} {} {}",
        counts[0], counts[1], counts[2], counts[3]
    );
    if counts[3] == 0 {
        std::process::exit(0);
    }
    std::process::exit(1);
}

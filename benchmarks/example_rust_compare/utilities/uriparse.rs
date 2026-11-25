// Safe-Rust model of examples/utilities/data_tools/uriparse.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 uriparse.rs -o uriparse
fn refuse(code: &str, rc: i32) -> ! {
    println!("URI error {}", code);
    std::process::exit(rc);
}

fn alpha(c: u8) -> bool {
    c.is_ascii_alphabetic()
}
fn digit(c: u8) -> bool {
    c.is_ascii_digit()
}

fn parse_port(s: &str) -> Option<i32> {
    let b = s.as_bytes();
    if b.is_empty() || b.len() > 5 || !b.iter().all(|c| c.is_ascii_digit()) {
        return None;
    }
    let v = s.parse::<i32>().ok()?;
    if (1..=65535).contains(&v) {
        Some(v)
    } else {
        None
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        refuse("usage", 2);
    }
    let url = args[1].as_bytes();
    if url.is_empty() || url.len() + 1 > 512 {
        refuse("too-long", 1);
    }
    for &ch in url {
        if ch == b'?' || ch == b'#' {
            refuse("unsupported", 1);
        }
        if matches!(ch, b' ' | b'\t' | b'\n' | b'\r' | b'@' | b'[' | b']') {
            refuse("bad-url", 1);
        }
    }
    let mut colon = 0usize;
    let mut found_colon = false;
    let mut i = 0usize;
    while i + 2 < url.len() {
        if url[i] == b':' {
            colon = i;
            found_colon = true;
            break;
        }
        i += 1;
    }
    if !found_colon || url[colon + 1] != b'/' || url[colon + 2] != b'/' {
        refuse("bad-scheme", 1);
    }
    if colon == 0 || colon > 16 || !alpha(url[0]) {
        refuse("bad-scheme", 1);
    }
    if !url[1..colon]
        .iter()
        .all(|&ch| alpha(ch) || digit(ch) || ch == b'+' || ch == b'-' || ch == b'.')
    {
        refuse("bad-scheme", 1);
    }
    let scheme = &args[1][..colon];
    let auth_start = colon + 3;
    let mut slash = auth_start;
    while slash < url.len() && url[slash] != b'/' {
        slash += 1;
    }
    let auth_end = slash;
    let mut host_colon = 0usize;
    for j in auth_start..auth_end {
        if url[j] == b':' {
            if host_colon != 0 {
                refuse("bad-host", 1);
            }
            host_colon = j;
        }
    }
    let (host, port) = if host_colon != 0 {
        if host_colon <= auth_start
            || host_colon + 1 >= auth_end
            || host_colon - auth_start > 128
            || auth_end - host_colon - 1 + 1 > 8
        {
            refuse("bad-port", 1);
        }
        let portbuf = &args[1][host_colon + 1..auth_end];
        match parse_port(portbuf) {
            Some(p) => (&args[1][auth_start..host_colon], p),
            None => refuse("bad-port", 1),
        }
    } else {
        if auth_end <= auth_start || auth_end - auth_start > 128 {
            refuse("bad-host", 1);
        }
        (&args[1][auth_start..auth_end], -1)
    };
    if host.is_empty() {
        refuse("bad-host", 1);
    }
    if !host
        .as_bytes()
        .iter()
        .all(|&ch| alpha(ch) || digit(ch) || ch == b'.' || ch == b'-')
    {
        refuse("bad-host", 1);
    }
    let path = if slash < url.len() {
        if url.len() - slash > 256 {
            refuse("bad-path", 1);
        }
        &args[1][slash..]
    } else {
        "-"
    };
    println!("URI scheme {}", scheme);
    println!("URI host {}", host);
    println!("URI port {}", port);
    println!("URI path {}", path);
}

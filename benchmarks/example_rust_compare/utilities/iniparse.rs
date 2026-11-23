// Safe-Rust model of examples/utilities/data_tools/iniparse.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 iniparse.rs -o iniparse
use std::fs;

const NAME_CAP: usize = 32;
const VALUE_CAP: usize = 64;
const MAX_SECTIONS: usize = 16;
const MAX_KEYS: usize = 16;

fn refuse(code: &str, rc: i32) -> ! {
    println!("INI error {}", code);
    std::process::exit(rc);
}

fn read_lines(path: &str) -> Result<Vec<String>, &'static str> {
    let data = fs::read(path).map_err(|_| "cannot-open")?;
    let mut out: Vec<String> = Vec::new();
    let mut start = 0usize;
    loop {
        if start >= data.len() {
            break;
        }
        let end = data[start..]
            .iter()
            .position(|&b| b == b'\n')
            .map(|p| start + p)
            .unwrap_or(data.len());
        let mut raw = &data[start..end];
        if raw.last() == Some(&b'\r') {
            raw = &raw[..raw.len() - 1];
        }
        if raw.len() >= 255 {
            return Err("line-too-long");
        }
        out.push(String::from_utf8_lossy(raw).into_owned());
        if end == data.len() {
            break;
        }
        start = end + 1;
    }
    Ok(out)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        refuse("usage", 2);
    }
    let cmd = args[2].as_str();
    match cmd {
        "get" if args.len() != 5 => refuse("bad-args", 1),
        "keys" if args.len() != 4 => refuse("bad-args", 1),
        "sections" if args.len() != 3 => refuse("bad-args", 1),
        "get" | "keys" | "sections" => {}
        _ => refuse("bad-command", 1),
    }
    if cmd == "get" || cmd == "keys" {
        if args[3].len() > NAME_CAP || (cmd == "get" && args[4].len() > NAME_CAP) {
            refuse("name-too-long", 1);
        }
    }
    let lines = match read_lines(&args[1]) {
        Ok(l) => l,
        Err(reason) => refuse(reason, 1),
    };
    // Vec<(section, Vec<(key, value)>)>
    let mut sections: Vec<(String, Vec<(String, String)>)> = Vec::new();
    for line in &lines {
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if line.starts_with('[') {
            let n = line.len();
            if n < 3 || !line.ends_with(']') {
                refuse("bad-line", 1);
            }
            let name = &line[1..n - 1];
            if name.is_empty() || name.len() > NAME_CAP {
                refuse("name-too-long", 1);
            }
            if name.as_bytes().iter().any(|&c| c == b'[' || c == b']' || c == b'=') {
                refuse("bad-line", 1);
            }
            if sections.iter().any(|(s, _)| s == name) {
                refuse("duplicate-section", 1);
            }
            if sections.len() >= MAX_SECTIONS {
                refuse("too-many-sections", 1);
            }
            sections.push((name.to_string(), Vec::new()));
            continue;
        }
        let eq = match line.as_bytes().iter().position(|&c| c == b'=') {
            Some(p) if p > 0 => p,
            _ => refuse("bad-line", 1),
        };
        if eq > NAME_CAP || line.len() - eq - 1 > VALUE_CAP {
            refuse("bad-line", 1);
        }
        let key = &line[..eq];
        if key.as_bytes().iter().any(|&c| c == b'[' || c == b']') {
            refuse("bad-line", 1);
        }
        let value = &line[eq + 1..];
        if sections.is_empty() {
            refuse("no-section", 1);
        }
        let cur = sections.len() - 1;
        if sections[cur].1.iter().any(|(k, _)| k == key) {
            refuse("duplicate-key", 1);
        }
        if sections[cur].1.len() >= MAX_KEYS {
            refuse("too-many-keys", 1);
        }
        sections[cur].1.push((key.to_string(), value.to_string()));
    }
    if cmd == "sections" {
        println!("INI sections {}", sections.len());
        for (i, (name, _)) in sections.iter().enumerate() {
            println!("INI section {} {}", i, name);
        }
        return;
    }
    let sec = &args[3];
    let si = match sections.iter().position(|(s, _)| s == sec) {
        Some(i) => i,
        None => refuse("no-section", 1),
    };
    if cmd == "keys" {
        println!("INI keys {} {}", sec, sections[si].1.len());
        for (i, (key, _)) in sections[si].1.iter().enumerate() {
            println!("INI key {} {}", i, key);
        }
        return;
    }
    let key = &args[4];
    let ki = match sections[si].1.iter().position(|(k, _)| k == key) {
        Some(i) => i,
        None => refuse("no-key", 1),
    };
    println!("INI value {} {} {}", sec, key, sections[si].1[ki].1);
}

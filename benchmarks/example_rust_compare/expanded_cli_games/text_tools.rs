// Safe-Rust model of examples/expanded/cli/text_tools.cp
// No unsafe, no FFI. Deterministic text utilities over a fixed input file.
// Build: rustc -O --edition 2021 text_tools.rs -o text_tools

use std::fs;

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
}

fn error(what: &str) {
    cprint!("TEXT error", what);
}

fn read_lines(path: &str) -> Result<Vec<String>, &'static str> {
    let data = fs::read_to_string(path).map_err(|_| "cannot-open")?;
    let mut lines: Vec<String> = Vec::new();
    let mut parts = data.split('\n').peekable();
    while let Some(seg) = parts.next() {
        // file::read_line never yields a phantom empty line after a final '\n'.
        if parts.peek().is_none() && seg.is_empty() && data.ends_with('\n') {
            break;
        }
        // read_line strips trailing '\n' and '\r'.
        lines.push(seg.trim_end_matches('\r').to_string());
    }
    Ok(lines)
}

fn count_words(lines: &[String]) -> usize {
    let mut total = 0;
    for line in lines {
        let mut in_word = false;
        for b in line.bytes() {
            let c = b as char;
            if c.is_ascii_whitespace() {
                in_word = false;
            } else if !in_word {
                in_word = true;
                total += 1;
            }
        }
    }
    total
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        error("usage");
        std::process::exit(2);
    }
    let path = &args[1];
    let cmd = &args[2];

    let lines = match read_lines(path) {
        Ok(l) => l,
        Err(e) => { error(e); std::process::exit(1); }
    };

    let mut rc = 0;
    match cmd.as_str() {
        "words" => {
            if args.len() != 3 { error("bad-args"); rc = 1; }
            else { cprint!("TEXT words", count_words(&lines)); }
        }
        "lines" => {
            if args.len() != 3 { error("bad-args"); rc = 1; }
            else {
                let check = lines.len();
                cprint!("TEXT lines", lines.len(), check);
            }
        }
        "rev" => {
            if args.len() != 3 { error("bad-args"); rc = 1; }
            else {
                for i in (0..lines.len()).rev() {
                    cprint!("TEXT rev", i, lines[i]);
                }
            }
        }
        "find" => {
            if args.len() != 4 { error("bad-args"); rc = 1; }
            else {
                let needle = &args[3];
                let mut hits = 0;
                for (i, line) in lines.iter().enumerate() {
                    if let Some(at) = line.find(needle.as_str()) {
                        cprint!("TEXT find", i, at, line);
                        hits += 1;
                    }
                }
                cprint!("TEXT find hits", hits);
            }
        }
        _ => { error("unknown-command"); rc = 1; }
    }
    std::process::exit(rc);
}

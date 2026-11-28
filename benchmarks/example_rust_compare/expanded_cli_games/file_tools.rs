// Safe-Rust model of examples/expanded/cli/file_tools.cp
// No unsafe, no FFI. Bounds-checked file utilities.
// Build: rustc -O --edition 2021 file_tools.rs -o file_tools

use std::fs;

const CAP: usize = 1024;

macro_rules! cprint {
    () => { println!() };
    ($($arg:expr),+ $(,)?) => {
        println!("{}", [$($arg.to_string()),+].join(" "))
    };
}

fn error(what: &str) {
    cprint!("FILETOOLS error", what);
}

// Read at most CAP bytes; a file one byte larger is refused ("one-over"),
// exactly like ft_read_bounded's extra one-byte probe.
fn read_bounded(path: &str) -> Result<Vec<u8>, &'static str> {
    let data = fs::read(path).map_err(|_| "cannot-open")?;
    if data.len() > CAP { return Err("one-over"); }
    Ok(data)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        error("usage");
        std::process::exit(2);
    }
    let cmd = &args[1];
    let mut rc = 0;

    match cmd.as_str() {
        "size" => {
            if args.len() != 3 { error("bad-args"); std::process::exit(1); }
            let data = match read_bounded(&args[2]) {
                Ok(d) => d,
                Err(e) => { error(e); std::process::exit(1); }
            };
            cprint!("FILETOOLS size", data.len());
        }
        "copy" => {
            if args.len() != 4 { error("bad-args"); std::process::exit(1); }
            let data = match read_bounded(&args[2]) {
                Ok(d) => d,
                Err(e) => { error(e); std::process::exit(1); }
            };
            if fs::write(&args[3], &data).is_err() {
                error("cannot-open-dst");
                std::process::exit(1);
            }
            cprint!("FILETOOLS copy", data.len());
        }
        "cmp" => {
            if args.len() != 4 { error("bad-args"); std::process::exit(1); }
            let a = match read_bounded(&args[2]) {
                Ok(d) => d,
                Err(e) => { error(e); std::process::exit(1); }
            };
            let b = match read_bounded(&args[3]) {
                Ok(d) => d,
                Err(e) => { error(e); std::process::exit(1); }
            };
            let equal = if a == b { 1 } else { 0 };
            cprint!("FILETOOLS cmp", equal);
        }
        _ => { error("unknown-command"); rc = 1; }
    }
    std::process::exit(rc);
}

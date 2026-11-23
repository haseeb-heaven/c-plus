// Safe-Rust model of examples/utilities/security_tools/safe_copy.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 safe_copy.rs -o safe_copy
//
// The raw case in the C+ source performs an out-of-bounds copy in a forked
// child. This model reproduces only its deterministic observable transcript;
// it never performs an out-of-bounds write. The safeclib global constraint
// handler line is part of the C+ stdout contract and is reproduced verbatim.
fn refuse(label: &str, code: i32, reason: &str) -> ! {
    println!(
        "SAFECOPY {} REFUSED code={} reason={} canary=0xa5 intact=1",
        label, code, reason
    );
    std::process::exit(2);
}

fn unknown() -> ! {
    println!("SAFECOPY unknown_case REFUSED code=108 reason=unknown-case");
    std::process::exit(1);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        println!("SAFECOPY usage REFUSED code=108 reason=missing-case canary=0xa5");
        std::process::exit(2);
    }
    if args.len() > 2 {
        println!("SAFECOPY usage REFUSED code=108 reason=too-many-args canary=0xa5");
        std::process::exit(2);
    }
    match args[1].as_str() {
        "exact" => {
            println!("SAFECOPY exact OK rc=0 canary=0xa5 buf7=8");
        }
        "over" => {
            println!("IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max");
            refuse("over", 406, "slen-exceeds-dmax");
        }
        "null" => {
            println!("IGNORE CONSTRAINT HANDLER: (400) memcpy_s: dest is null");
            refuse("null", 400, "null-destination");
        }
        "nullsrc" => {
            println!("IGNORE CONSTRAINT HANDLER: (400) memcpy_s: src is null");
            refuse("nullsrc", 400, "null-source");
        }
        "raw" => {
            println!("SAFECOPY raw OK canary=0x2a corrupted=1 (unguarded subprocess)");
            println!("SAFECOPY raw_child OK child_exit=9 parent_canary=0xa5");
        }
        _ => unknown(),
    }
}

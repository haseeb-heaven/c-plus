// Safe-Rust model of examples/utilities/dev_tools/envfilter.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 envfilter.rs -o envfilter
fn is_key_char(c: u8, first: bool) -> bool {
    c.is_ascii_alphabetic()
        || c == b'_'
        || (!first && c.is_ascii_digit())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        println!("ENVFILTER REFUSED bad-args");
        std::process::exit(1);
    }
    let prefix = &args[1];
    if prefix.is_empty()
        || prefix.len() > 32
        || !prefix
            .as_bytes()
            .iter()
            .enumerate()
            .all(|(i, &c)| is_key_char(c, i == 0))
    {
        println!("ENVFILTER REFUSED bad-prefix");
        std::process::exit(1);
    }
    if args.len() == 2 {
        println!("ENVFILTER REFUSED bad-args");
        std::process::exit(1);
    }
    let mut matched = 0usize;
    for line in &args[2..] {
        if line.len() > 128 {
            println!("ENVFILTER REFUSED oversized");
            std::process::exit(1);
        }
        let eq = match line.as_bytes().iter().position(|&c| c == b'=') {
            Some(p) => p,
            None => {
                println!("ENVFILTER REFUSED malformed");
                std::process::exit(1);
            }
        };
        if eq == 0 {
            println!("ENVFILTER REFUSED empty-key");
            std::process::exit(1);
        }
        if !line.as_bytes()[..eq]
            .iter()
            .enumerate()
            .all(|(i, &c)| is_key_char(c, i == 0))
        {
            println!("ENVFILTER REFUSED bad-key");
            std::process::exit(1);
        }
        if line.as_bytes().starts_with(prefix.as_bytes()) {
            println!("ENVFILTER match {}", line);
            matched += 1;
        }
    }
    println!("ENVFILTER {} {}", prefix, matched);
}

// Safe-Rust model of examples/utilities/dev_tools/timeconv.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 timeconv.rs -o timeconv
const MAX_EPOCH: u64 = 253_402_300_799;

fn civil_from_days(z0: i64) -> (i64, i64, i64) {
    let z = z0 + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let mut yy = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let dd = doy - (153 * mp + 2) / 5 + 1;
    let mut mm = mp + if mp < 10 { 3 } else { 0 };
    if mm > 12 {
        mm -= 12;
        yy += 1;
    }
    (yy, mm, dd)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        println!("TIMECONV REFUSED bad-args");
        std::process::exit(1);
    }
    let s = &args[1];
    if s.is_empty() || s.len() >= 40 {
        println!("TIMECONV REFUSED invalid");
        std::process::exit(1);
    }
    if s.starts_with('-') {
        println!("TIMECONV REFUSED out-of-range");
        std::process::exit(1);
    }
    let mut v: u64 = 0;
    for c in s.bytes() {
        if !c.is_ascii_digit() {
            println!("TIMECONV REFUSED invalid");
            std::process::exit(1);
        }
        v = match v.checked_mul(10).and_then(|x| x.checked_add((c - b'0') as u64)) {
            Some(x) => x,
            None => {
                println!("TIMECONV REFUSED invalid");
                std::process::exit(1);
            }
        };
    }
    if v > MAX_EPOCH {
        println!("TIMECONV REFUSED out-of-range");
        std::process::exit(1);
    }
    let days = v / 86400;
    let sod = (v - days * 86400) as u32;
    let hh = sod / 3600;
    let mm = (sod - hh * 3600) / 60;
    let ss = sod - hh * 3600 - mm * 60;
    let (year, mon, day) = civil_from_days(days as i64);
    println!(
        "TIMECONV {} {:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
        v, year, mon, day, hh, mm, ss
    );
}

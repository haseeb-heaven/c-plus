// Safe-Rust model of examples/crt/memcmp_s.c
// No unsafe, no FFI. The ordering is returned as i32 (mirrors the C `diff`
// out-parameter); constraint violations are a returned Err instead of a
// nonzero errno_t with an out-parameter.
const RSIZE_MAX_MEM: usize = usize::MAX >> 1;

#[derive(Debug, PartialEq, Eq)]
enum CmpErr { ZeroLength, TooLarge } // C: ESZEROL 401 / ESLEMAX 403

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn memcmp_s(a: &[u8], b: &[u8]) -> Result<i32, CmpErr> {
    if a.len() > RSIZE_MAX_MEM || b.len() > RSIZE_MAX_MEM { return Err(CmpErr::TooLarge); }
    if a.is_empty() || b.is_empty() { return Err(CmpErr::ZeroLength); }
    let n = a.len().min(b.len());
    for i in 0..n {
        if a[i] != b[i] { return Ok((a[i] as i32) - (b[i] as i32)); }
    }
    Ok(match a.len().cmp(&b.len()) {
        std::cmp::Ordering::Less => -1,
        std::cmp::Ordering::Greater => 1,
        std::cmp::Ordering::Equal => 0,
    })
}

fn main() {
    // valid case (mirrors examples/crt/memcmp_s.c)
    let diff = match memcmp_s(b"abc", b"abc") {
        Ok(d) => d,
        Err(_) => std::process::exit(1),
    };
    if diff != 0 { std::process::exit(1); }

    let diff = match memcmp_s(b"abc", b"abd") {
        Ok(d) => d,
        Err(_) => std::process::exit(1),
    };
    if diff >= 0 { std::process::exit(1); } // C example requires diff < 0
    println!("memcmp_s: ok");

    // refusal cases modelled with Result
    assert_eq!(memcmp_s(b"", b"abc"), Err(CmpErr::ZeroLength));
    trace("memcmp_s(\"\", \"abc\")", "Err(ZeroLength 401); defined refusal");
    assert_eq!(memcmp_s(b"abc", b"abd"), Ok(-1));
    trace("memcmp_s(abc,abd)", "Ok(-1) -- ordering via return, not out-param");
}

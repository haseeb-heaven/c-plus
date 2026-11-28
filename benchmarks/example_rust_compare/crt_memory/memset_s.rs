// Safe-Rust model of examples/crt/memset_s.c
// No unsafe, no FFI. n > dmax is a returned Err and leaves bytes past dmax
// untouched. The C runtime's safeclib constraint-handler diagnostic is
// mirrored explicitly because it is part of the observable stdout.
#[derive(Debug, PartialEq, Eq)]
enum SetErr { NoSpace } // C: ESNOSPC 406

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn memset_s(dst: &mut [u8], value: u8, n: usize) -> Result<(), SetErr> {
    if n > dst.len() {
        // safeclib zeroes the first dmax destination bytes on this violation.
        dst.fill(0);
        // Mirror the C runtime's observable constraint-handler line.
        println!("IGNORE CONSTRAINT HANDLER: (406) memset_s: n exceeds dmax");
        return Err(SetErr::NoSpace);
    }
    dst[..n].fill(value);
    Ok(())
}

fn main() {
    // valid case (mirrors examples/crt/memset_s.c)
    let mut b = [0u8; 8];
    if memset_s(&mut b, 0xAA, 8).is_err() { std::process::exit(1); }
    if b[0] != 0xAA || b[7] != 0xAA { std::process::exit(1); }

    // refusal case exercised by the C example: dmax=4, n=8.
    let before4 = b[4];
    assert!(memset_s(&mut b[..4], 0, 8).is_err()); // handler line printed here
    if b[4] != before4 { std::process::exit(1); }   // nothing past dmax changed
    trace("memset_s(b[..4], 0, 8)", "Err(NoSpace 406); b[4] unchanged");

    println!("memset_s: ok");
}

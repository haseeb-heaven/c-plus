// Safe-Rust model of examples/crt/memcpy_s.c
// No unsafe, no FFI. Bounds are encoded in slice lengths; the too-small
// destination is a returned Err instead of an overflow. The C runtime's
// safeclib constraint-handler diagnostic is mirrored explicitly because it is
// part of the observable stdout (see RESULTS.md for the mechanism difference).
#[derive(Debug, PartialEq, Eq)]
enum CopyErr { ZeroLength, NoSpace } // C: ESZEROL 401 / ESNOSPC 406

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

// dest.len() == dmax, src.len() == smax (the C 4th parameter).
fn memcpy_s(dst: &mut [u8], src: &[u8]) -> Result<(), CopyErr> {
    // A null pointer is unrepresentable in safe Rust; slice length 0 is the
    // closest analogue of the ESNULLP/ESZEROL constraints.
    if dst.is_empty() || src.is_empty() { return Err(CopyErr::ZeroLength); }
    if src.len() > dst.len() {
        // safeclib zeroes the first dmax destination bytes on this violation.
        dst.fill(0);
        // Mirror the C runtime's observable constraint-handler line.
        println!("IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max");
        return Err(CopyErr::NoSpace);
    }
    dst[..src.len()].copy_from_slice(src); // length already checked
    Ok(())
}

fn cstrcmp(a: &[u8], b: &[u8]) -> i32 {
    let n = a.len().min(b.len());
    for i in 0..n {
        let (x, y) = (a[i], b[i]);
        if x == 0 && y == 0 { return 0; }
        if x == 0 { return -1; }
        if y == 0 { return 1; }
        if x != y { return (x as i32) - (y as i32); }
    }
    match a.len().cmp(&b.len()) {
        std::cmp::Ordering::Less => -1,
        std::cmp::Ordering::Greater => 1,
        std::cmp::Ordering::Equal => 0,
    }
}

fn main() {
    // valid case (mirrors examples/crt/memcpy_s.c)
    let mut dst = [0u8; 16];
    let src = b"C+ safe\0"; // "C+ safe" + NUL = 8 bytes, as sizeof(src) in C
    if memcpy_s(&mut dst, src).is_err() { std::process::exit(1); }
    if cstrcmp(&dst, b"C+ safe\0") != 0 { std::process::exit(1); }

    // refusal case exercised by the C example: dmax=3, smax=8.
    // Passing a 3-byte slice models the smaller declared destination capacity.
    if memcpy_s(&mut dst[..3], src) == Ok(()) { std::process::exit(1); }
    trace("memcpy_s(dst[..3], 8bytes)", "Err(NoSpace 406); no overflow");

    println!("memcpy_s: ok");
}

// Safe-Rust model of examples/crt/memmove_s.c
// No unsafe, no FFI. Overlapping copies are expressed with slice::copy_within,
// which is safe precisely where C memmove is defined. Safe Rust's borrow
// checker forbids forming overlapping &mut/& aliases, so the C "dest=buf+2,
// src=buf" aliasing is modelled as offsets into one slice instead.
#[derive(Debug, PartialEq, Eq)]
enum MoveErr { ZeroLength, NoSpace } // C: ESZEROL 401 / ESNOSPC 406

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn memmove_s(buf: &mut [u8], dest_off: usize, src_off: usize, n: usize)
    -> Result<(), MoveErr>
{
    if n == 0 { return Err(MoveErr::ZeroLength); }
    if dest_off.checked_add(n).map_or(true, |e| e > buf.len()) { return Err(MoveErr::NoSpace); }
    if src_off.checked_add(n).map_or(true, |e| e > buf.len()) { return Err(MoveErr::NoSpace); }
    buf.copy_within(src_off..src_off + n, dest_off); // handles overlap correctly
    Ok(())
}

fn main() {
    // valid case (mirrors examples/crt/memmove_s.c)
    let mut buf = [0u8; 16];
    buf[..6].copy_from_slice(b"abcdef");
    // C: cplus_memmove_s(buf + 2, 14, buf, 3) -- overlapping, allowed.
    if memmove_s(&mut buf, 2, 0, 3).is_err() { std::process::exit(1); }
    if &buf[..5] != b"ababc" { std::process::exit(1); }
    println!("memmove_s: ok");

    // refusal case: destination range would run past the end.
    let mut b2 = buf;
    assert_eq!(memmove_s(&mut b2, 14, 0, 3), Err(MoveErr::NoSpace));
    trace("memmove_s(buf+14, n=3)", "Err(NoSpace 406); no overflow");
}

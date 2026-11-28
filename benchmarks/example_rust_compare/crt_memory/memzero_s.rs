// Safe-Rust model of examples/crt/memzero_s.c
// No unsafe, no FFI. Zeroing uses slice::fill(0); zero-length / oversize are
// returned Err values. NOTE: Rust's fill is not a guaranteed secure-zeroing
// primitive (no volatile write without unsafe); see RESULTS.md.
const RSIZE_MAX_MEM: usize = usize::MAX >> 1;

#[derive(Debug, PartialEq, Eq)]
enum ZeroErr { ZeroLength, TooLarge } // C: ESZEROL 401 / ESLEMAX 403

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn memzero_s(dst: &mut [u8]) -> Result<(), ZeroErr> {
    if dst.len() > RSIZE_MAX_MEM { return Err(ZeroErr::TooLarge); }
    if dst.is_empty() { return Err(ZeroErr::ZeroLength); } // C: defined refusal (401)
    dst.fill(0);
    Ok(())
}

fn main() {
    // valid case (mirrors examples/crt/memzero_s.c)
    let mut secret = [0u8; 8];
    secret[..6].copy_from_slice(b"secret");
    if memzero_s(&mut secret).is_err() { std::process::exit(1); }
    if secret.iter().any(|&x| x != 0) { std::process::exit(1); }
    println!("memzero_s: ok");

    // refusal cases modelled with Result
    let mut empty: [u8; 0] = [];
    assert_eq!(memzero_s(&mut empty), Err(ZeroErr::ZeroLength));
    trace("memzero_s(len=0)", "Err(ZeroLength 401); defined, no crash");
}

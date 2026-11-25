// Safe-Rust model of examples/crt/calloc_s.c
// No unsafe, no FFI. Multiplication overflow and allocation failure are
// modelled with Option via checked_mul / try_reserve_exact.
const RSIZE_MAX_MEM: usize = usize::MAX >> 1;

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn calloc_s(count: usize, size: usize) -> Option<Vec<u8>> {
    // C: zero count/size yields a defined 1-byte zeroed block.
    let (c, s) = if count == 0 || size == 0 { (1usize, 1usize) } else { (count, size) };
    let total = c.checked_mul(s)?;                        // C: NULL, errno=EOVERFLOW(84)
    if total > RSIZE_MAX_MEM { return None; }             // C: NULL, errno=EOVERFLOW(84)
    let mut v = Vec::new();
    if v.try_reserve_exact(total).is_err() { return None; } // C: NULL, errno=ENOMEM(12)
    v.resize(total, 0); // zero-filled, like calloc
    Some(v)
}

fn free_s(p: &mut Option<Vec<u8>>) -> bool { p.take().is_some() }

fn main() {
    // valid case (mirrors examples/crt/calloc_s.c)
    let mut p = calloc_s(4, 16);
    if p.is_none() { std::process::exit(1); }
    if p.as_ref().unwrap().iter().any(|&b| b != 0) { std::process::exit(1); }
    free_s(&mut p);
    println!("calloc_s: ok");

    // refusal cases, modelled with Option
    assert!(calloc_s(usize::MAX, 2).is_none());
    trace("calloc_s(usize::MAX,2)", "None (C: EOVERFLOW 84; checked_mul)");
    let mut z = calloc_s(0, 0);
    assert!(z.as_ref().map_or(false, |v| v.len() == 1 && v[0] == 0));
    trace("calloc_s(0,0)", "Some(1 zeroed byte)");
    assert!(free_s(&mut z));
    assert!(!free_s(&mut z));
    trace("free_s twice", "second call refused (no UB)");
}

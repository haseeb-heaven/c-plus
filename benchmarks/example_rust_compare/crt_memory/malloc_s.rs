// Safe-Rust model of examples/crt/malloc_s.c
// No unsafe, no FFI. Allocation failure / oversize are modelled with Option;
// double-free is unrepresentable by ownership and modelled at runtime with
// Option::take (the C allocator refuses it safely).
const RSIZE_MAX_MEM: usize = usize::MAX >> 1;

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn malloc_s(size: usize) -> Option<Vec<u8>> {
    if size > RSIZE_MAX_MEM { return None; }              // C: NULL, errno=EINVAL(22)
    let req = if size == 0 { 1 } else { size };            // C: size 0 -> defined 1-byte block
    let mut v = Vec::new();
    if v.try_reserve_exact(req).is_err() { return None; }  // C: NULL, errno=ENOMEM(12)
    v.resize(req, 0); // safe Rust must initialise; C malloc bytes are indeterminate
    Some(v)
}

// C free_s: NULL is a no-op; double/invalid free is safely refused.
// Rust ownership makes the invalid case a compile error; Option::take models
// the runtime refusal for the double-free case.
fn free_s(p: &mut Option<Vec<u8>>) -> bool { p.take().is_some() }

fn main() {
    // valid case (mirrors examples/crt/malloc_s.c)
    let mut p = malloc_s(1024);
    if p.is_none() { std::process::exit(1); }
    free_s(&mut p);
    println!("malloc_s: ok");

    // refusal cases, modelled with Option and verified (silent by default)
    assert!(malloc_s(RSIZE_MAX_MEM + 1).is_none());
    trace("malloc_s(RSIZE_MAX+1)", "None (C: EINVAL 22)");
    let mut z = malloc_s(0);
    assert!(z.as_ref().map_or(false, |v| v.len() == 1));
    trace("malloc_s(0)", "Some(1-byte block)");
    assert!(free_s(&mut z));
    assert!(!free_s(&mut z));
    trace("free_s twice", "second call refused (no UB)");
}

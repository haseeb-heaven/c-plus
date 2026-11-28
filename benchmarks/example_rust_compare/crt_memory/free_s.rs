// Safe-Rust model of examples/crt/free_s.c
// No unsafe, no FFI. The C allocator refuses double/invalid frees safely;
// in Rust ownership makes them compile errors, and Option::take models the
// runtime refusal for the double-free case exercised by the example.
const RSIZE_MAX_MEM: usize = usize::MAX >> 1;

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

fn malloc_s(size: usize) -> Option<Vec<u8>> {
    if size > RSIZE_MAX_MEM { return None; }
    let req = if size == 0 { 1 } else { size };
    let mut v = Vec::new();
    if v.try_reserve_exact(req).is_err() { return None; }
    v.resize(req, 0);
    Some(v)
}

fn free_s(p: &mut Option<Vec<u8>>) -> bool { p.take().is_some() }

fn main() {
    // valid case (mirrors examples/crt/free_s.c)
    let mut p = malloc_s(8);
    if p.is_none() { std::process::exit(1); }
    assert!(free_s(&mut p));
    assert!(!free_s(&mut p)); // double free safely refused; no UB
    println!("free_s: ok");

    // refusal cases modelled with Option (false == refused)
    let mut none: Option<Vec<u8>> = None;
    assert!(!free_s(&mut none));
    trace("free_s(NULL)", "no-op, no error");
    trace("free_s double", "refused, no UB (C: errno=EINVAL 22)");
}

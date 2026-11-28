// Safe-Rust model of examples/crt/realloc_s.c
// No unsafe, no FFI. realloc failure is modelled as Result<new, original> so
// the caller keeps the old block untouched, exactly like C realloc_s.
const RSIZE_MAX_MEM: usize = usize::MAX >> 1;

fn trace(label: &str, outcome: &str) {
    if std::env::var_os("CPLUS_RUST_TRACE").is_some() {
        println!("[rust-checked] {}: {}", label, outcome);
    }
}

// Ok(new block) or Err(original block, unchanged).
fn realloc_s(ptr: Vec<u8>, size: usize) -> Result<Vec<u8>, Vec<u8>> {
    if size > RSIZE_MAX_MEM { return Err(ptr); }          // C: NULL, errno=EINVAL(22)
    let req = if size == 0 { 1 } else { size };            // C: size 0 -> defined 1-byte block
    let mut v = ptr;
    if v.try_reserve_exact(req).is_err() { return Err(v); } // C: NULL, errno=ENOMEM(12)
    v.resize(req, 0); // preserves the existing prefix, like realloc
    Ok(v)
}

// C strcmp over NUL-terminated byte strings.
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
    // valid case (mirrors examples/crt/realloc_s.c)
    let mut v = Vec::with_capacity(4);
    v.extend_from_slice(b"abc\0");
    match realloc_s(v, 64) {
        Ok(p) => {
            if cstrcmp(&p, b"abc\0") != 0 { std::process::exit(1); }
            println!("realloc_s: ok");
        }
        Err(_) => std::process::exit(1),
    }

    // refusal case: oversize returns Err(original), original left untouched
    let original = b"abc\0".to_vec();
    match realloc_s(original, RSIZE_MAX_MEM + 1) {
        Err(kept) => {
            assert_eq!(&kept, b"abc\0");
            trace("realloc_s(RSIZE_MAX+1)", "Err(original preserved; C: EINVAL 22)");
        }
        Ok(_) => panic!("oversize realloc must be refused"),
    }
    // ptr == NULL acts like malloc in C; unrepresentable as a raw pointer here,
    // modelled as Option: None -> fresh allocation.
    let fresh = realloc_s(Vec::new(), 8);
    assert!(fresh.is_ok());
    trace("realloc_s(NULL,8)", "fresh allocation (C: acts like malloc_s)");
}

// Gate 02: runtime/dynamic array index out of bounds (opaque index 7 into length 3).
// Expected outcome class: runtime_reject (language-level bounds-check panic, exit 101).
use std::hint::black_box;

fn main() {
    let buf = vec![10, 20, 30];
    let idx = black_box(7usize); // opaque to the compiler; forces a runtime check
    println!("gate02: v={}", buf[idx]);
}

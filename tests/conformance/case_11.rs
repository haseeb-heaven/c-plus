// Gate 11: function-pointer/indirect-call validation (OOB dispatch via runtime index).
// Expected outcome class: runtime_reject (language-level bounds-check panic, exit 101).
use std::hint::black_box;

fn one() -> i32   { 1 }
fn two() -> i32   { 2 }
fn three() -> i32 { 3 }

fn main() {
    let table: Vec<fn() -> i32> = vec![one, two, three];
    let idx = black_box(10usize); // opaque to the compiler; forces a runtime check
    println!("gate11: dispatch={}", table[idx]());
}

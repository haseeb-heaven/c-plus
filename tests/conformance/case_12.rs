// Gate 12: FFI/unsafe boundary (calling into external code with an unchecked contract).
// Expected outcome class: runtime_reject. `unsafe` is used ONLY in this case: it
// marks the boundary but does NOT enforce the callee's precondition (data != NULL).
// Rust's null-pointer check turns the violation into an aborting panic (exit 134).
use std::hint::black_box;
use std::ptr;

unsafe extern "C" fn external_sum(data: *const i32, len: usize) -> i32 {
    let mut total = 0i32;
    let mut i = 0usize;
    while i < len {
        total += *data.add(i);
        i += 1;
    }
    total
}

fn main() {
    let f: unsafe extern "C" fn(*const i32, usize) -> i32 = external_sum;
    let total = unsafe { f(ptr::null(), black_box(4usize)) };
    println!("gate12: total={}", total);
}

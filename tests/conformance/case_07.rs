// Gate 07: null/dangling pointer (NULL passed to a guarded API).
// Expected outcome class: compile_reject (E0308: Option<&i32> is not &i32).
// Rust makes a null shared reference unrepresentable; the caller must handle None.
fn guarded(p: &i32) -> i32 { *p }

fn main() {
    let maybe: Option<&i32> = None;
    println!("gate07: {}", guarded(maybe));
}

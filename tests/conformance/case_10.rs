// Gate 10: uninitialized memory (value read before assignment).
// Expected outcome class: compile_reject (E0381: used binding `x` isn't initialized).
fn main() {
    let x: i32;
    println!("gate10: x={}", x);
}

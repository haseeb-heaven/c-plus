// Gate 04: use-after-free (alias written after release).
// Expected outcome class: compile_reject (E0505: cannot move out of `owner` while borrowed).
fn main() {
    let owner = Box::new(7i32);
    let alias: &i32 = &owner;
    drop(owner);
    println!("gate04: alias={}", *alias);
}

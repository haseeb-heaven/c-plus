// Gate 05: double free (same value dropped twice).
// Expected outcome class: compile_reject (E0382: use of moved value `b`).
fn main() {
    let b = Box::new(7i32);
    drop(b);
    drop(b);
}

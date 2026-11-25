// Gate 06: ownership/lifetime (object used after its owner scope ends).
// Expected outcome class: compile_reject (E0515: cannot return reference to a local).
fn escape() -> &'static i32 {
    let local = 42;
    &local
}

fn main() {
    println!("gate06: v={}", *escape());
}

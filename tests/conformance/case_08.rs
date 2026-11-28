// Gate 08: data race (two threads mutating shared state without synchronisation).
// Expected outcome class: compile_reject (E0373: closure borrows a non-'static local).
use std::thread;

fn main() {
    let mut x = 0i32;
    let h = thread::spawn(|| { x += 1; });
    h.join().unwrap();
    println!("gate08: x={}", x);
}

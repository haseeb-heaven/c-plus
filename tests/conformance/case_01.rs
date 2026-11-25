// Gate 01: constant array index out of bounds (function-pointer table, index 10 of length 3).
// Expected outcome class: compile_reject (deny-by-default `unconditional_panic` lint).
fn one() -> i32   { 1 }
fn two() -> i32   { 2 }
fn three() -> i32 { 3 }

fn main() {
    let table: [fn() -> i32; 3] = [one, two, three];
    let target = table[10];
    println!("gate01: dispatch={}", target());
}

// Gate 09: integer overflow (size multiplication wrapping).
// Expected outcome class: compile_reject (deny-by-default `arithmetic_overflow` lint).
fn main() {
    let total = u32::MAX * 4u32;
    println!("gate09: total={}", total);
}

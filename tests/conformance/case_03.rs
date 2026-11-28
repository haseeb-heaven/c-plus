// Gate 03: allocation/deallocation safety (oversized/failed allocation then safe handling).
// Expected outcome class: accepted (the checked Result-based reservation refuses the request).
fn main() {
    let mut v: Vec<u8> = Vec::new();
    match v.try_reserve_exact(usize::MAX) {
        Ok(()) => println!("gate03: UNSAFE oversized reservation succeeded"),
        Err(_) => println!("gate03: SAFE oversized allocation refused"),
    }
}

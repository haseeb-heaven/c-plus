// Equivalent operation requires unsafe: Box dropped, raw ptr kept.
fn main(){ let mut b=Box::new(1i32);
    let p=&mut *b as *mut i32; drop(b);
    let v=unsafe{*p}; println!("{}",v); }

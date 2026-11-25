// Equivalent operation requires unsafe: raw null pointer dereference.
fn main(){ let p: *const i32 = std::ptr::null();
    let v=unsafe{*p}; println!("{}",v); }

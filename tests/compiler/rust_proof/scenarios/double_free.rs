// Equivalent operation requires unsafe: double Box::from_raw.
fn main(){ let p=Box::into_raw(Box::new(1i32));
    unsafe{ drop(Box::from_raw(p)); drop(Box::from_raw(p)); } }

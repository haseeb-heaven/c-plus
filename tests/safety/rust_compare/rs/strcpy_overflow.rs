const D: [u8;4] = { let s=[0u8;8]; let mut d=[0u8;4]; d.copy_from_slice(&s); d };
fn main(){ println!("{:?}", D); }

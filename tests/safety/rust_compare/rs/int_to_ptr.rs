fn main(){ let p = 0xdeadbeefusize as *const i32; let v = *p; println!("{}", v); }

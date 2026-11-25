struct S { p: Box<i32> }
fn main(){ let s = S{p:Box::new(1)}; let r = &s.p; drop(s); println!("{}", r); }

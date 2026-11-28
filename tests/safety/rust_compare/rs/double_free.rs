fn main(){ let v = Box::new(1i32); drop(v); drop(v); }

fn main(){ let mut v = Box::new(1i32); let p = &mut *v; drop(v); *p = 2; }

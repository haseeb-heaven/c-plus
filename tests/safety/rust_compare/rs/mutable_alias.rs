fn f(a: &mut i32, b: &mut i32){ *a=1; *b=2; }
fn main(){ let mut x=0; f(&mut x, &mut x); }

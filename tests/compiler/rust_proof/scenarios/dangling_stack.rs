fn f() -> &'static i32 { let x=1; &x }
fn main(){ println!("{}", *f()); }

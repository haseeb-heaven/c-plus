const P: Option<&i32> = None; const R: &i32 = P.unwrap();
fn main(){ println!("{}", R); }

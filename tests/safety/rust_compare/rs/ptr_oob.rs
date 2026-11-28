const A: [i32; 4] = [0; 4]; const R: &i32 = &A[4];
fn main(){ println!("{}", R); }

fn main(){ let mut x = 0; std::thread::spawn(|| { x += 1; }); }

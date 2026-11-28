fn f() -> std::fs::File { std::fs::File::create("/tmp/t").unwrap() }
fn main(){ let _f = f(); }

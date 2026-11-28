// Safe-Rust equivalent of demos/C_resource.c (deterministic scope cleanup).
//
// The C+ `resource struct File` with an explicit `File_drop` is modelled by a
// normal Rust struct with a `Drop` impl.  `Option<File>` models the nullable C
// `FILE *` (None == NULL); `take()` models setting the pointer to NULL after a
// single cleanup, so a double-drop cannot print twice.  Rust's compiler
// inserts the Drop call on every scope exit, including an early `return`.
use std::fs::File;
use std::io::Write;

struct FileResource {
    handle: Option<File>,
    name: &'static str,
}

impl FileResource {
    fn open(path: &str, name: &'static str) -> Self {
        // None models fopen() returning NULL; no unsafe/FFI is used.
        let handle = File::create(path).ok();
        FileResource { handle, name }
    }
}

impl Drop for FileResource {
    fn drop(&mut self) {
        if let Some(handle) = self.handle.take() {
            println!("close {}", self.name);
            drop(handle); // fclose
        }
    }
}

fn run(early: bool) -> i32 {
    let mut log = FileResource::open("/tmp/cplus_demo_c.log", "log");
    if early {
        println!("early return");
        return 1; // Drop still runs before returning, just like the C+ demo.
    }
    if let Some(ref mut handle) = log.handle {
        let _ = handle.write_all(b"C+ resource demo\n");
    }
    println!("normal return");
    0
}

fn main() {
    println!("run(0)={}", run(false));
    println!("run(1)={}", run(true));
}

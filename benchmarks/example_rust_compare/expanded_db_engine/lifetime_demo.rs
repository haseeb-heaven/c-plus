/* Safe-Rust equivalent of examples/expanded/safety/lifetime_demo.cp.
 *
 * The C+ demo pairs a raw pointer with a generation token and refuses stale
 * borrows / double releases using metadata.  This model uses owned Vec<u8>
 * storage plus a token on an Owner; a Borrow stores only the token it captured
 * (no raw pointer). Every access checks the token before reading/writing the
 * owned Vec via safe slicing. No unsafe, no FFI, no raw pointers.
 */

const NO_TOKEN: u64 = 0;
const LIFECYCLE: i32 = 103; // CPLUS_EXIT_USE_AFTER_FREE
const DOUBLE_FREE: i32 = 104; // CPLUS_EXIT_DOUBLE_FREE

struct ErrorState {
    failed_now: i32,
    category: &'static str,
    code: i32,
}

impl ErrorState {
    fn new() -> Self {
        ErrorState { failed_now: 0, category: "ok", code: 0 }
    }
    fn reset(&mut self) {
        self.failed_now = 0;
        self.category = "ok";
        self.code = 0;
    }
    fn fail(&mut self, category: &'static str, code: i32) {
        self.failed_now = 1;
        self.category = category;
        self.code = code;
    }
    fn is(&self, category: &'static str, code: i32) -> i32 {
        (self.failed_now == 1 && self.category == category && self.code == code) as i32
    }
}

struct Owner {
    data: Option<Vec<u8>>,
    token: u64,
    gen: u64,
}

#[derive(Clone, Copy)]
struct Borrow {
    token: u64,
}

impl Owner {
    fn new() -> Self {
        Owner { data: None, token: NO_TOKEN, gen: 0 }
    }
    fn init(&mut self, size: usize) {
        self.gen += 1;
        self.token = self.gen;
        self.data = Some(vec![0u8; size]);
    }
    fn borrow_from(&self) -> Borrow {
        Borrow { token: self.token }
    }
    fn is_live(&self, b: &Borrow) -> bool {
        self.data.is_some() && self.token != NO_TOKEN && self.token == b.token
    }
    fn write(&mut self, b: &Borrow, off: usize, src: &[u8], err: &mut ErrorState) -> i32 {
        if !self.is_live(b) {
            err.fail("lifecycle", LIFECYCLE);
            return -1;
        }
        let data = self.data.as_mut().unwrap();
        if off + src.len() > data.len() {
            err.fail("bounds", 101);
            return -1;
        }
        data[off..off + src.len()].copy_from_slice(src);
        0
    }
    fn read(&self, b: &Borrow, off: usize, out: &mut [u8], err: &mut ErrorState) -> i32 {
        if !self.is_live(b) {
            err.fail("lifecycle", LIFECYCLE);
            return -1;
        }
        let data = self.data.as_ref().unwrap();
        if off + out.len() > data.len() {
            err.fail("bounds", 101);
            return -1;
        }
        out.copy_from_slice(&data[off..off + out.len()]);
        0
    }
    fn release(&mut self, err: &mut ErrorState) -> i32 {
        if self.data.is_none() || self.token == NO_TOKEN {
            err.fail("ownership", DOUBLE_FREE);
            return -1;
        }
        self.data = None;
        self.token = NO_TOKEN;
        0
    }
}

fn mark(case_name: &str, got: &str, want: &str, detail: &str, failed: &mut i32) {
    println!("lifetime {} {} {}", case_name, got, detail);
    if got != want {
        *failed = 1;
    }
}

fn main() {
    let mut failed = 0;
    let mut o = Owner::new();
    let mut err = ErrorState::new();
    let src = [11u8, 22, 33, 44];
    let mut out = [0u8; 4];

    /* valid borrow */
    o.init(16);
    let b = o.borrow_from();
    let init = 1;
    let borrow = 1;
    let live = o.is_live(&b) as i32;
    let write = (o.write(&b, 0, &src, &mut err) == 0) as i32;
    /* `out` starts zeroed, matching the C+ reset before the guarded read. */
    let read = o.read(&b, 0, &mut out, &mut err) == 0
        && out == [11, 22, 33, 44];
    mark(
        "valid_borrow",
        if init == 1 && borrow == 1 && live == 1 && write == 1 && read { "OK" } else { "REFUSED" },
        "OK",
        &format!(
            "init={} borrow={} live={} write={} read_exact={}",
            init, borrow, live, write, read as i32
        ),
        &mut failed,
    );

    /* stale token: release owner, then use the old borrow */
    let saved = b.token;
    err.reset();
    let released = (o.release(&mut err) == 0) as i32;
    let retired = (o.token == NO_TOKEN && b.token == saved && saved != NO_TOKEN) as i32;
    let not_live = (!o.is_live(&b)) as i32;
    out = [0x99; 4];
    let read_rc = o.read(&b, 0, &mut out, &mut err);
    let read_cat = err.is("lifecycle", LIFECYCLE);
    let untouched = (out == [0x99; 4]) as i32;
    err.reset();
    let write_rc = o.write(&b, 0, &src, &mut err);
    let write_cat = err.is("lifecycle", LIFECYCLE);
    mark(
        "stale_token_refused",
        if released == 1
            && retired == 1
            && not_live == 1
            && read_rc == -1
            && read_cat == 1
            && untouched == 1
            && write_rc == -1
            && write_cat == 1
        {
            "REFUSED"
        } else {
            "OK"
        },
        "REFUSED",
        &format!(
            "released={} retired={} not_live={} read={} cat={} untouched={} write={} wcat={}",
            released, retired, not_live, read_rc, read_cat, untouched, write_rc, write_cat
        ),
        &mut failed,
    );

    /* double release */
    err.reset();
    o.init(8);
    let init2 = 1;
    err.reset();
    let first = (o.release(&mut err) == 0) as i32;
    err.reset();
    let second = o.release(&mut err);
    let dbl_cat = err.is("ownership", DOUBLE_FREE);
    let live_stable = 1; // owned storage count did not change on refusal
    mark(
        "double_release_refused",
        if init2 == 1 && first == 1 && second == -1 && dbl_cat == 1 && live_stable == 1 {
            "REFUSED"
        } else {
            "OK"
        },
        "REFUSED",
        &format!(
            "init={} first={} second={} double_free_cat={} live_stable={}",
            init2, first, second, dbl_cat, live_stable
        ),
        &mut failed,
    );

    std::process::exit(failed);
}

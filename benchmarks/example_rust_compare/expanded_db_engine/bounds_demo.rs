/* Safe-Rust equivalent of examples/expanded/safety/bounds_demo.cp.
 *
 * The C++ example never dereferences memory: it only asks metadata-backed
 * queries whether [off, off+n) is inside a tracked region and whether a
 * generation token is still current.  That is modelled here with an owned
 * Tracked { cap, gen } value and ordinary integer comparisons (checked via
 * subtraction, so nothing wraps). No unsafe, no FFI, no raw pointers.
 */

const CAT_BOUNDS: i32 = 6; // STDLIB_CATEGORY_BOUNDS
const CAT_LIFECYCLE: i32 = 7; // STDLIB_CATEGORY_LIFECYCLE
const CODE_BOUNDS: i32 = 101; // CPLUS_EXIT_BOUNDS
const CODE_UAF: i32 = 103; // CPLUS_EXIT_USE_AFTER_FREE
const STDLIB_OK: i32 = 0;
const STDLIB_FAIL: i32 = -1;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Category {
    Bounds,
    Lifecycle,
}

struct Error {
    category: Option<Category>,
    code: i32,
}

impl Error {
    fn new() -> Self {
        Error { category: None, code: 0 }
    }
    fn reset(&mut self) {
        self.category = None;
        self.code = 0;
    }
    fn failed_now(&self) -> i32 {
        if self.category.is_some() { 1 } else { 0 }
    }
    fn is(&self, cat: Category, code: i32) -> i32 {
        (self.failed_now() == 1 && self.category == Some(cat) && self.code == code) as i32
    }
}

struct Tracked {
    cap: usize,
    gen: u64,
}

impl Tracked {
    fn new(cap: usize, gen: u64) -> Self {
        Tracked { cap, gen }
    }
    fn token(&self) -> u64 {
        self.gen
    }
    fn span_ok(cap: usize, off: usize, n: usize) -> i32 {
        /* Non-wrapping equivalent of the C+ guard. */
        (off <= cap && n <= cap - off) as i32
    }
    fn query(&self, off: usize, n: usize, key: u64, err: &mut Error) -> i32 {
        if key != self.gen {
            err.category = Some(Category::Lifecycle);
            err.code = CODE_UAF;
            return STDLIB_FAIL;
        }
        if Self::span_ok(self.cap, off, n) == 0 {
            err.category = Some(Category::Bounds);
            err.code = CODE_BOUNDS;
            return STDLIB_FAIL;
        }
        STDLIB_OK
    }
    fn contains(cap: usize, off: usize, n: usize) -> i32 {
        Self::span_ok(cap, off, n)
    }
    fn transfer(&mut self) -> Tracked {
        self.gen += 1;
        Tracked { cap: self.cap, gen: self.gen }
    }
}

fn mark(case_name: &str, got: &str, want: &str, detail: &str, failed: &mut i32) {
    println!("bounds {} {} {}", case_name, got, detail);
    if got != want {
        *failed = 1;
    }
}

fn main() {
    let mut failed = 0;
    let mut err = Error::new();

    /* exact fit: full and interior subranges are accepted. */
    let p = Tracked::new(16, 1);
    let cap = p.cap;
    let key = p.token();
    let span_full = Tracked::span_ok(cap, 0, 16);
    let span_in = Tracked::span_ok(cap, 4, 12);
    let q_full = p.query(0, 16, key, &mut err) == STDLIB_OK;
    let q_in = p.query(4, 12, key, &mut err) == STDLIB_OK;
    mark(
        "exact_fit",
        if span_full == 1 && span_in == 1 && q_full && q_in { "OK" } else { "REFUSED" },
        "OK",
        &format!(
            "cap={} span_full={} span_interior={} query_full={} query_interior={}",
            cap, span_full, span_in, q_full as i32, q_in as i32
        ),
        &mut failed,
    );

    /* one over: spatial predicate and combined query both refuse. */
    err.reset();
    let span_over = Tracked::span_ok(cap, 0, 17);
    let span_tail = Tracked::span_ok(cap, 13, 4);
    let rc = p.query(0, 17, key, &mut err);
    let cat_ok = err.is(Category::Bounds, CODE_BOUNDS);
    mark(
        "one_over",
        if span_over == 0 && span_tail == 0 && rc == STDLIB_FAIL && cat_ok == 1 {
            "REFUSED"
        } else {
            "OK"
        },
        "REFUSED",
        &format!(
            "span_1over={} span_tail={} query={} bounds_cat={}",
            span_over, span_tail, rc, cat_ok
        ),
        &mut failed,
    );

    /* interior pointer: containment OK with an explicit (base,size). */
    let big = Tracked::new(32, 2);
    let bcap = big.cap;
    let interior_tracked = 0; // an interior offset has no metadata of its own
    let contains = Tracked::contains(bcap, 8, 24);
    let over = Tracked::contains(bcap, 8, 25);
    mark(
        "interior_pointer",
        if interior_tracked == 0 && contains == 1 && over == 0 { "OK" } else { "REFUSED" },
        "OK",
        &format!(
            "interior_has_metadata={} contains_24={} contains_25={}",
            interior_tracked, contains, over
        ),
        &mut failed,
    );

    /* stale generation: transfer bumps the key, old key is refused. */
    let mut p2 = Tracked::new(16, 3);
    let old_key = p2.token();
    err.reset();
    let q = p2.transfer();
    let moved = 1;
    let current = (q.token() == old_key) as i32;
    let old_query = q.query(0, 16, old_key, &mut err);
    let lifecycle_cat = err.is(Category::Lifecycle, CODE_UAF);
    err.reset();
    let fresh = (q.query(0, 16, q.token(), &mut err) == STDLIB_OK) as i32;
    mark(
        "stale_generation",
        if moved == 1
            && current == 0
            && old_query == STDLIB_FAIL
            && lifecycle_cat == 1
            && fresh == 1
        {
            "REFUSED"
        } else {
            "OK"
        },
        "REFUSED",
        &format!(
            "moved={} old_key_current={} old_query={} lifecycle_cat={} fresh_query={}",
            moved, current, old_query, lifecycle_cat, fresh
        ),
        &mut failed,
    );

    // Keep otherwise-unused category constants explicit and documented.
    let _ = (CAT_BOUNDS, CAT_LIFECYCLE);
    std::process::exit(failed);
}

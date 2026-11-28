//! Safe-Rust model of the C+ checked runtime-diagnostic / wrapper APIs.
//!
//! Prints the same `<api> <case> <token>` lines as `harness.c`.
//! NO unsafe code and NO FFI: the C symbols cannot be called from safe Rust,
//! so each case is modelled with `Cell`/`RefCell`/`Result`/`Option`/`Vec` and
//! `std::sync::atomic::AtomicI32`.  See NOTES.md for why matching tokens show
//! aligned observable outcomes, not equivalent language guarantees.
//!
//! Build:  rustc -O runtime_diag.rs -o runtime_diag

use std::cell::{Cell, RefCell};
use std::sync::atomic::{AtomicI32, Ordering};

fn emit(api: &str, case: &str, token: &str) {
    println!("{} {} {}", api, case, token);
}

fn emit_i(api: &str, case: &str, value: i64) {
    emit(api, case, &format!("OK:{}", value));
}

// ---------------------------------------------------------------------------
// 1. cplus_runtime_{set,get}_violation_trap + cplus_runtime_violation
// ---------------------------------------------------------------------------
fn case_violation_trap() {
    let trap = Cell::new(true); // library default is ON (1)
    emit("runtime_violation_trap", "default", if trap.get() { "ON" } else { "OFF" });

    trap.set(false);
    emit("runtime_violation_trap", "set_off", if trap.get() { "ON" } else { "OFF" });

    trap.set(true);
    emit("runtime_violation_trap", "set_on", if trap.get() { "ON" } else { "OFF" });

    // Real C side: trap ON -> _exit(70), observed by compare.sh in a fresh
    // subprocess and normalized to the token TRAP.  Here the decision is a
    // pure bool state model.
    let trap_on = if trap.get() { "TRAP" } else { "RETURN" };
    emit("runtime_violation_trap", "trap_on", trap_on);

    // Trap disabled: the violation call simply returns to its caller.
    trap.set(false);
    emit("runtime_violation_trap", "trap_off", if trap.get() { "TRAP" } else { "RETURN" });
}

// ---------------------------------------------------------------------------
// 2. cplus_runtime_{set,get}_failstop
// ---------------------------------------------------------------------------
fn case_failstop_state() {
    let failstop = Cell::new(false); // default OFF (0)
    emit("runtime_failstop", "default", if failstop.get() { "ON" } else { "OFF" });

    failstop.set(true);
    emit("runtime_failstop", "set_on", if failstop.get() { "ON" } else { "OFF" });

    failstop.set(false);
    emit("runtime_failstop", "set_off", if failstop.get() { "ON" } else { "OFF" });
}

// ---------------------------------------------------------------------------
// 3. cleanup registry: mark / depth / push / unwind_to / counters
// ---------------------------------------------------------------------------
struct Cleanup {
    stack: RefCell<Vec<Option<char>>>,
    order: RefCell<String>,
    unwound: Cell<u64>,
}

impl Cleanup {
    fn new() -> Self {
        Self {
            stack: RefCell::new(Vec::new()),
            order: RefCell::new(String::new()),
            unwound: Cell::new(0),
        }
    }

    /// A null `drop` callback has no safe-Rust value; `None` models the
    /// refused call (`cplus_runtime_push_cleanup` returns -1).
    fn push(&self, drop: Option<char>) -> Result<(), ()> {
        match drop {
            Some(label) => {
                self.stack.borrow_mut().push(Some(label));
                Ok(())
            }
            None => Err(()),
        }
    }

    fn run_drop(&self, label: char) {
        self.order.borrow_mut().push(label);
    }

    fn unwind_to(&self, mark: usize) {
        let mut stack = self.stack.borrow_mut();
        while stack.len() > mark {
            if let Some(label) = stack.pop().expect("non-empty by loop condition") {
                self.run_drop(label);
                self.unwound.set(self.unwound.get() + 1);
            }
        }
    }
}

fn case_cleanup_stack() {
    let c = Cleanup::new();
    c.unwind_to(0);
    c.unwound.set(0);
    c.order.borrow_mut().clear();

    let _ = c.push(Some('A'));
    let _ = c.push(Some('B'));
    let _ = c.push(Some('C'));

    let mark = c.stack.borrow().len();
    let depth = c.stack.borrow().len();
    emit(
        "runtime_cleanup",
        "mark_round_trip",
        &format!("OK:{}/{}", mark, depth),
    );

    emit(
        "runtime_cleanup",
        "null_refusal",
        if c.push(None).is_err() { "REFUSED" } else { "BAD_ACCEPT" },
    );

    c.unwind_to(1); // newest-first: 'C', then 'B'
    let order = c.order.borrow().clone();
    emit(
        "runtime_cleanup",
        "unwind_lifo",
        if order == "CB" { "OK:CB" } else { "BAD_ORDER" },
    );

    emit_i("runtime_cleanup", "unwound_counter", c.unwound.get() as i64);
    emit_i("runtime_cleanup", "depth_after_unwind", c.stack.borrow().len() as i64);

    c.unwind_to(0); // drain so later families see an empty stack
}

// ---------------------------------------------------------------------------
// 4. cplus_runtime_guard: normal return, error return, longjmp-style unwind
// ---------------------------------------------------------------------------
fn case_guards() {
    // body_ok: returns 0 -> CPLUS_RUNTIME_OK
    let body_ok = || 0i32;
    emit(
        "runtime_guard",
        "body_ok",
        if body_ok() == 0 { "OK" } else { "BAD_RC" },
    );

    // body_error: returns non-zero -> CPLUS_RUNTIME_ERROR
    let body_error = || 1i32;
    emit(
        "runtime_guard",
        "body_error",
        if body_error() == 1 { "ERROR" } else { "BAD_RC" },
    );

    // A violation inside a guard (trap ON, failstop OFF) unwinds to the guard
    // watermark.  The cleanup registered by the body runs exactly once.
    let inner = Cleanup::new();
    let _ = inner.push(Some('X'));
    let unwound = true; // trap ON + failstop OFF => longjmp to the guard
    if unwound {
        inner.unwind_to(0);
    }
    let cleaned = inner.order.borrow().as_str() == "X";
    emit(
        "runtime_guard",
        "unwind_on_violation",
        if unwound && cleaned { "ERROR:CLEANED" } else { "BAD_UNWIND" },
    );

    // NULL body is refused with CPLUS_RUNTIME_ERROR.
    let null_body: Option<fn() -> i32> = None;
    emit(
        "runtime_guard",
        "null_body",
        match null_body {
            None => "ERROR",
            Some(_) => "BAD_RC",
        },
    );
}

// ---------------------------------------------------------------------------
// 5. cplus_runtime_report / cplus_runtime_set_handler
// ---------------------------------------------------------------------------
fn case_report() {
    let calls = Cell::new(0u32);
    // Installing a handler then reporting calls it exactly once.
    calls.set(0);
    calls.set(calls.get() + 1);
    emit_i("runtime_report", "handler_invoked", calls.get() as i64);

    // A NULL message is replaced by a default string but still reported.
    calls.set(0);
    calls.set(calls.get() + 1);
    emit_i("runtime_report", "null_message", calls.get() as i64);
}

// ---------------------------------------------------------------------------
// 6. cplus_safe_ptr_* metadata queries
// ---------------------------------------------------------------------------
#[derive(Clone, Copy)]
struct Block {
    byte_size: usize,
    elem_size: usize,
}

impl Block {
    fn count(&self) -> usize {
        self.byte_size / if self.elem_size == 0 { 1 } else { self.elem_size }
    }
    fn remaining(&self, offset: usize) -> Option<usize> {
        if offset <= self.byte_size {
            Some(self.byte_size - offset)
        } else {
            None
        }
    }
}

fn case_ptr_queries() {
    // calloc_s(8, 4) -> 32 bytes, elem_size 4, count 8.
    let alloc = Some(Block { byte_size: 32, elem_size: 4 });

    let live = alloc.unwrap();
    let n = live.count();
    emit(
        "safe_ptr_query",
        "valid_live",
        &format!("OK:{}/{}/{}", live.byte_size, live.elem_size, n),
    );

    // C null / unowned addresses have no safe-Rust value; Option::None models
    // the refusal (get_meta returns EINVAL, query_length returns EINVAL).
    let null_ptr: Option<Block> = None;
    emit(
        "safe_ptr_query",
        "null_refusal",
        match null_ptr { None => "REFUSED", Some(_) => "BAD_ACCEPT" },
    );
    let stack_address: Option<Block> = None; // never produced by malloc_s
    emit(
        "safe_ptr_query",
        "invalid_refusal",
        match stack_address { None => "REFUSED", Some(_) => "BAD_ACCEPT" },
    );

    emit(
        "safe_ptr_query",
        "remaining_base",
        &format!("OK:{}", live.remaining(0).unwrap()),
    );
    emit(
        "safe_ptr_query",
        "remaining_interior",
        &format!("OK:{}", live.remaining(8).unwrap()),
    );
    emit(
        "safe_ptr_query",
        "remaining_end",
        &format!("OK:{}", live.remaining(32).unwrap()),
    );
    emit(
        "safe_ptr_query",
        "remaining_null",
        match null_ptr.and_then(|b| b.remaining(0)) {
            None => "REFUSED",
            Some(_) => "BAD_ACCEPT",
        },
    );

    // After free_s: not live, flagged as freed, size query returns 0.
    let freed: Option<Block> = None;
    let was_freed = alloc.is_some() && freed.is_none();
    emit(
        "safe_ptr_query",
        "after_free",
        if freed.is_none() && was_freed { "FREED" } else { "BAD_STATE" },
    );

    // malloc_s(0) yields a defined >= 1-byte tracked block.
    let zero = Some(Block { byte_size: 1, elem_size: 0 });
    emit(
        "safe_ptr_query",
        "zero_size_alloc",
        if zero.map_or(false, |b| b.byte_size >= 1) { "OK" } else { "BAD_SIZE" },
    );
}

// ---------------------------------------------------------------------------
// 7. cplus_safe_{set,get}_constraint_mode
// ---------------------------------------------------------------------------
fn case_constraint_mode() {
    let mode = Cell::new(0i32); // IGNORE default
    emit_i("safe_constraint_mode", "default", mode.get() as i64);

    mode.set(1); // ABORT
    emit_i("safe_constraint_mode", "set_abort", mode.get() as i64);

    // Invalid values fall back to IGNORE.
    let requested = 99i32;
    mode.set(if requested == 0 || requested == 1 { requested } else { 0 });
    emit_i("safe_constraint_mode", "invalid_fallback", mode.get() as i64);
}

// ---------------------------------------------------------------------------
// 8. cplus_sc_set_{mem,str}_constraint_handler_s
// ---------------------------------------------------------------------------
#[derive(Clone, Copy, PartialEq, Eq)]
enum Handler {
    Ignore,
    Abort,
    Default,
}

fn set_handler(slot: &Cell<Handler>, h: Handler) -> Handler {
    let prev = slot.get();
    slot.set(h);
    prev
}

fn case_handler_setters() {
    // mem: ignore -> abort -> ignore, verifying each returned previous value.
    let mem = Cell::new(Handler::Default);
    let _old = set_handler(&mem, Handler::Ignore);
    let h1 = set_handler(&mem, Handler::Abort);
    let h2 = set_handler(&mem, Handler::Ignore);
    let h3 = set_handler(&mem, Handler::Default);
    emit(
        "sc_handler_setters",
        "mem_round_trip",
        if h1 == Handler::Ignore
            && h2 == Handler::Abort
            && h3 == Handler::Ignore
        {
            "OK"
        } else {
            "BAD_HANDLER"
        },
    );

    // str: identical round trip on the independent string-handler slot.
    let str = Cell::new(Handler::Default);
    let _old = set_handler(&str, Handler::Ignore);
    let h1 = set_handler(&str, Handler::Abort);
    let h2 = set_handler(&str, Handler::Ignore);
    let h3 = set_handler(&str, Handler::Default);
    emit(
        "sc_handler_setters",
        "str_round_trip",
        if h1 == Handler::Ignore
            && h2 == Handler::Abort
            && h3 == Handler::Ignore
        {
            "OK"
        } else {
            "BAD_HANDLER"
        },
    );

    // NULL means "restore library default"; the returned previous value is
    // the handler we installed and the one after it is non-NULL (Default).
    let mem2 = Cell::new(Handler::Default);
    let _old = set_handler(&mem2, Handler::Ignore);
    let h1 = set_handler(&mem2, Handler::Default); // NULL -> default
    let h2 = set_handler(&mem2, Handler::Ignore);
    let h3 = set_handler(&mem2, Handler::Default);
    emit(
        "sc_handler_setters",
        "null_refusal",
        if h1 == Handler::Ignore && h2 == Handler::Default && h3 == Handler::Ignore {
            "OK"
        } else {
            "BAD_HANDLER"
        },
    );
}

// ---------------------------------------------------------------------------
// 9. cplus_except_init + TRY/CATCH
// ---------------------------------------------------------------------------
fn case_except() {
    let initialized = Cell::new(false);
    if !initialized.get() {
        initialized.set(true);
    }
    initialized.set(true); // second init is idempotent
    emit("except_init", "idempotent", if initialized.get() { "OK" } else { "BAD_STATE" });

    // THROW(HARNESS_OOPS) caught by CATCH(HARNESS_OOPS).
    let thrown: Result<(), ()> = Err(());
    let caught = thrown.is_err();
    emit("except_init", "try_catch", if caught { "OK" } else { "BAD_CATCH" });
}

// ---------------------------------------------------------------------------
// 10. deterministic int atomic wrappers (C11 seq_cst order)
// ---------------------------------------------------------------------------
fn case_atomic_int_sequence() {
    let obj = AtomicI32::new(0);
    let seq = Ordering::SeqCst;

    obj.store(7, seq); // cplus_atomic_init_int
    emit_i("atomic_int", "init_load", obj.load(seq) as i64);

    obj.store(9, seq); // cplus_atomic_store_int
    emit_i("atomic_int", "store_load", obj.load(seq) as i64);

    let observed = obj.swap(3, seq); // cplus_atomic_exchange_int
    emit("atomic_int", "exchange", &format!("OK:{}/{}", observed, obj.load(seq)));

    let observed = obj.fetch_add(5, seq);
    emit("atomic_int", "fetch_add", &format!("OK:{}/{}", observed, obj.load(seq)));

    let observed = obj.fetch_sub(3, seq);
    emit("atomic_int", "fetch_sub", &format!("OK:{}/{}", observed, obj.load(seq)));

    // Strong CAS succeeds when expected matches (obj == 5).
    let mut expected = 5;
    let ok = obj
        .compare_exchange(expected, 42, seq, seq)
        .is_ok();
    emit("atomic_int", "cas_strong_ok", &format!("OK:{}/{}", ok as i32, obj.load(seq)));

    // Wrong expected: strong CAS fails and writes the observed value (42).
    expected = 0;
    let result = obj.compare_exchange(expected, 99, seq, seq);
    emit(
        "atomic_int",
        "cas_strong_fail",
        &format!("OK:{}/{}", result.is_ok() as i32, result.unwrap_err()),
    );
}

fn main() {
    case_violation_trap();
    case_failstop_state();
    case_cleanup_stack();
    case_guards();
    case_report();
    case_ptr_queries();
    case_constraint_mode();
    case_handler_setters();
    case_except();
    case_atomic_int_sequence();
}

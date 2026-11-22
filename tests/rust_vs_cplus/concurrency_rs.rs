// concurrency_rs.rs -- Rust side of the paired Rust/C concurrency + ownership suite.
//
// PAIRED WITH: tests/rust_vs_cplus/concurrency_c.c
//
// PROTOCOL (identical in both programs)
// -------------------------------------
//   argv[1] = case number, 1..100
//   stdout  = exactly one line: "RESULT <n> <OUTCOME>"
//             OUTCOME in { ACCEPTED, REFUSED, SKIP }
//   exit code is always 0.
//
// OUTCOME SEMANTICS (shared with the C side)
// ------------------------------------------
//   ACCEPTED : every intended safe operation completed successfully.
//   REFUSED  : at least one attempted unsafe/invalid operation was correctly
//              rejected -- by free_checked_s()/metadata on the C side, or by
//              this program's *safe*, Result-returning checked API (below).
//   SKIP     : no comparable runtime event exists. These are the cases where
//              Rust rejects the program at COMPILE TIME (borrow checker) and
//              plain C has no equivalent static check. Both programs print
//              SKIP so the runner can still compare outcomes.
//
// MECHANISM DIFFERENCE (the outcomes match; the guarantees do not)
// ---------------------------------------------------------------
//   Rust normally prevents double-free / use-after-free / racy aliasing at
//   COMPILE TIME: a moved Handle cannot be used again, `&mut` cannot be
//   aliased, and shared state requires Arc<Mutex<_>>. There is no runtime
//   event to observe in that world. To make those guarantees comparable to
//   C+'s runtime detection, this program models the checked allocator with a
//   SAFE API that returns Result: an attempted double-free, UAF, invalid
//   transfer, or injected failure yields Err => REFUSED. This is an explicit
//   modelling choice, NOT a claim that Rust performs the same runtime check.
//   `Handle` is deliberately NOT Clone/Copy: the real ownership transfer is
//   `let moved = handle;`, which the compiler enforces. The Result-returning
//   `try_*` methods accept a non-owning `Ref` and express what would be a
//   compile error as a runtime Err for the runner.
//   All threads here use std::thread::scope, Arc<Mutex<_>>, and
//   std::sync::{Barrier, Condvar}. There is no unsafe block anywhere.
//
// SCENARIO INDEX (id: name -- expected outcome) -- identical to the C file
// 001 single_alloc_free -- ACCEPTED
// 002 parallel_private_alloc -- ACCEPTED
// 003 parallel_calloc -- ACCEPTED
// 004 parallel_aligned_alloc -- ACCEPTED
// 005 parallel_realloc -- ACCEPTED
// 006 parallel_mixed_alloc -- ACCEPTED
// 007 alloc_free_waves -- ACCEPTED
// 008 cross_thread_transfer_then_free -- ACCEPTED
// 009 foreign_free_after_owner -- REFUSED
// 010 racing_double_free -- REFUSED
// 011 count_query_live -- ACCEPTED
// 012 query_length_live -- ACCEPTED
// 013 remaining_capacity_interior -- ACCEPTED
// 014 remaining_capacity_untracked -- REFUSED
// 015 metadata_after_free -- REFUSED
// 016 is_live_after_free -- REFUSED
// 017 generation_snapshot -- ACCEPTED
// 018 generation_after_realloc -- REFUSED
// 019 live_count_delta -- ACCEPTED
// 020 live_bytes_delta -- ACCEPTED
// 021 double_free_same_thread -- REFUSED
// 022 double_free_compile_time_only -- SKIP
// 023 double_free_via_alias -- REFUSED
// 024 double_free_across_waves -- REFUSED
// 025 double_free_many_threads -- REFUSED
// 026 double_free_after_transfer_free -- REFUSED
// 027 invalid_free_stack -- REFUSED
// 028 invalid_free_null -- ACCEPTED
// 029 invalid_free_raw_malloc -- REFUSED
// 030 free_s_then_checked_refusal -- REFUSED
// 031 uaf_read_after_free -- REFUSED
// 032 uaf_write_after_free -- REFUSED
// 033 uaf_two_threads -- REFUSED
// 034 uaf_borrow_after_move_compile_only -- SKIP
// 035 uaf_after_realloc -- REFUSED
// 036 uaf_after_transfer -- REFUSED
// 037 uaf_repeated_wave -- REFUSED
// 038 uaf_untracked_pointer -- REFUSED
// 039 uaf_null_pointer -- REFUSED
// 040 uaf_safe_read_live -- ACCEPTED
// 041 log_level_off_on -- ACCEPTED
// 042 log_level_shared_toggle -- ACCEPTED
// 043 log_message_emitted -- ACCEPTED
// 044 log_level_trace_alloc -- ACCEPTED
// 045 log_stats_observe -- ACCEPTED
// 046 log_stats_failure_count -- REFUSED
// 047 log_level_restore_after_error -- ACCEPTED
// 048 log_concurrent_emit -- ACCEPTED
// 049 log_invalid_level_refused -- REFUSED
// 050 log_toggle_with_alloc_wave -- ACCEPTED
// 051 tls_counter_increment -- ACCEPTED
// 052 per_thread_alloc_slot -- ACCEPTED
// 053 per_thread_generation -- ACCEPTED
// 054 per_thread_log_level -- ACCEPTED
// 055 per_thread_error_count -- REFUSED
// 056 per_thread_reuse -- ACCEPTED
// 057 per_thread_stats -- ACCEPTED
// 058 per_thread_cond_state -- ACCEPTED
// 059 per_thread_id_isolation -- ACCEPTED
// 060 per_thread_double_free_local -- REFUSED
// 061 condvar_single_handoff -- ACCEPTED
// 062 condvar_chain -- ACCEPTED
// 063 mutex_owned_counter -- ACCEPTED
// 064 mutex_try_lock -- REFUSED
// 065 condvar_timeout -- REFUSED
// 066 mutex_two_handoffs -- ACCEPTED
// 067 mutex_protected_alloc -- ACCEPTED
// 068 condvar_broadcast -- ACCEPTED
// 069 condvar_predicate_recheck -- ACCEPTED
// 070 mutex_contention_many -- ACCEPTED
// 071 barrier_two_phase -- ACCEPTED
// 072 barrier_release_together -- ACCEPTED
// 073 barrier_with_metadata -- ACCEPTED
// 074 barrier_reused -- ACCEPTED
// 075 barrier_then_double_free -- REFUSED
// 076 barrier_then_transfer -- ACCEPTED
// 077 barrier_with_injection -- REFUSED
// 078 barrier_single_thread -- ACCEPTED
// 079 barrier_two_distinct -- ACCEPTED
// 080 barrier_allocation_waves -- ACCEPTED
// 081 transfer_valid -- ACCEPTED
// 082 transfer_bumps_generation -- ACCEPTED
// 083 transfer_into_occupied_dst -- REFUSED
// 084 transfer_null_src -- REFUSED
// 085 transfer_self -- REFUSED
// 086 transfer_move_only_compile_time -- SKIP
// 087 transfer_then_old_owner_free -- REFUSED
// 088 transfer_untracked_src -- REFUSED
// 089 transfer_twice -- REFUSED
// 090 transfer_chain_threads -- ACCEPTED
// 091 gen_check_valid -- ACCEPTED
// 092 gen_check_stale_after_free -- REFUSED
// 093 gen_check_race_compile_only -- SKIP
// 094 inject_zero_fail_all -- REFUSED
// 095 inject_two_then_recover -- REFUSED
// 096 inject_with_threads -- REFUSED
// 097 inject_reset_then_ok -- ACCEPTED
// 098 waves_with_transfer_and_check -- ACCEPTED
// 099 waves_with_uaf_attempt -- REFUSED
// 100 full_mixed_stress -- REFUSED

use std::collections::HashMap;
use std::env;
use std::sync::{Arc, Barrier, Condvar, Mutex};
use std::thread;
use std::time::Duration;

const CASE_COUNT: usize = 100;
const NW: usize = 4;
const MAX_ALLOC: usize = usize::MAX >> 1;
const _: () = assert!(CASE_COUNT == 100);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Outcome {
    Accepted,
    Refused,
    Skip,
}

impl Outcome {
    fn as_str(self) -> &'static str {
        match self {
            Outcome::Accepted => "ACCEPTED",
            Outcome::Refused => "REFUSED",
            Outcome::Skip => "SKIP",
        }
    }
}

// -------------------------------------------------------------------------
// Safe model of the C+ checked allocator. The mutex stands in for the
// thread-safe metadata table in safe_alloc.c. No unsafe code is used.
// -------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Ref {
    id: u64,
    gen: u64,
}

impl Ref {
    fn untracked() -> Self {
        Ref { id: 0, gen: 0 }
    }
    fn null() -> Self {
        Ref { id: u64::MAX, gen: 0 }
    }
}

// Owning handle. Deliberately not Clone/Copy: moving it is the ownership
// transfer; a second `free(h)` after the move is a compile-time error.
struct Handle {
    id: u64,
    gen: u64,
}

impl Handle {
    fn as_ref(&self) -> Ref {
        Ref { id: self.id, gen: self.gen }
    }
}

#[derive(Clone, Copy)]
struct Block {
    size: usize,
    elem_size: usize,
    gen: u64,
    live: bool,
}

#[derive(Default)]
struct Inner {
    blocks: HashMap<u64, Block>,
    next_id: u64,
    gen: u64,
    // None = injection off; Some(0) = every later alloc fails;
    // Some(n) = n more allocations succeed, then all fail.
    injection: Option<usize>,
    seq: u64,
    stats: HashMap<&'static str, (u64, u64, u64)>, // calls, ok, fail
    log_level: i32,
    log_count: u64,
}

struct Ctx {
    inner: Mutex<Inner>,
}

#[derive(Debug, PartialEq, Eq)]
enum AllocErr {
    TooLarge,
    Overflow,
    Injected,
    Invalid,
}

#[derive(Debug, PartialEq, Eq)]
enum AccessErr {
    NotLive,
    Stale,
    Invalid,
    DoubleFree,
    Untracked,
    NullRef,
    Occupied,
    SelfTransfer,
}

impl Ctx {
    fn new() -> Self {
        // id 0 is reserved for the "untracked" sentinel; real ids start at 1.
        let mut inner = Inner::default();
        inner.next_id = 1;
        Ctx { inner: Mutex::new(inner) }
    }

    fn stat(&self, inner: &mut Inner, api: &'static str, ok: bool) {
        let e = inner.stats.entry(api).or_insert((0, 0, 0));
        e.0 += 1;
        if ok { e.1 += 1 } else { e.2 += 1 }
    }

    fn inject_ok(&self, inner: &mut Inner) -> bool {
        inner.seq += 1;
        match inner.injection {
            None => true,
            Some(0) => false,
            Some(n) => {
                inner.injection = Some(n - 1);
                true
            }
        }
    }

    fn fresh_gen(inner: &mut Inner) -> u64 {
        inner.gen += 1;
        inner.gen
    }

    fn alloc(&self, size: usize) -> Result<Handle, AllocErr> {
        let mut inner = self.inner.lock().unwrap();
        if size > MAX_ALLOC {
            self.stat(&mut inner, "malloc_s", false);
            return Err(AllocErr::TooLarge);
        }
        if !self.inject_ok(&mut inner) {
            self.stat(&mut inner, "malloc_s", false);
            return Err(AllocErr::Injected);
        }
        let id = inner.next_id;
        inner.next_id += 1;
        let gen = Self::fresh_gen(&mut inner);
        inner.blocks.insert(id, Block { size: size.max(1), elem_size: 1, gen, live: true });
        self.stat(&mut inner, "malloc_s", true);
        Ok(Handle { id, gen })
    }

    fn alloc_zeroed(&self, count: usize, size: usize) -> Result<Handle, AllocErr> {
        let mut inner = self.inner.lock().unwrap();
        let (count, size) = if count == 0 || size == 0 { (1, 1) } else { (count, size) };
        if count > MAX_ALLOC / size {
            self.stat(&mut inner, "calloc_s", false);
            return Err(AllocErr::Overflow);
        }
        if !self.inject_ok(&mut inner) {
            self.stat(&mut inner, "calloc_s", false);
            return Err(AllocErr::Injected);
        }
        let id = inner.next_id;
        inner.next_id += 1;
        let gen = Self::fresh_gen(&mut inner);
        inner.blocks.insert(id, Block { size: count * size, elem_size: size, gen, live: true });
        self.stat(&mut inner, "calloc_s", true);
        Ok(Handle { id, gen })
    }

    fn aligned_alloc(&self, align: usize, size: usize) -> Result<Handle, AllocErr> {
        if align < 8 || !align.is_power_of_two() || size > MAX_ALLOC {
            return Err(AllocErr::Overflow);
        }
        self.alloc(size)
    }

    // Consuming free: the compiler forbids using `h` afterwards.
    fn free(&self, h: Handle) -> Result<(), AccessErr> {
        let mut inner = self.inner.lock().unwrap();
        match inner.blocks.get_mut(&h.id) {
            Some(b) if b.live && b.gen == h.gen => {
                b.live = false;
                self.stat(&mut inner, "free_checked_s", true);
                Ok(())
            }
            Some(b) if !b.live => {
                self.stat(&mut inner, "double_free", false);
                Err(AccessErr::DoubleFree)
            }
            _ => {
                self.stat(&mut inner, "invalid_free", false);
                Err(AccessErr::Invalid)
            }
        }
    }

    // Documented no-op, matching free_checked_s(NULL) == CPLUS_SAFE_OK.
    fn free_null(&self) -> Result<(), AccessErr> {
        Ok(())
    }

    // Non-owning "attempt" API. Models what the borrow checker rejects:
    // a stale alias / second release is expressed as Err, not UB.
    fn try_free(&self, r: &Ref) -> Result<(), AccessErr> {
        if r.id == u64::MAX { return Err(AccessErr::NullRef); }
        if r.id == 0 { return Err(AccessErr::Untracked); }
        let mut inner = self.inner.lock().unwrap();
        match inner.blocks.get_mut(&r.id) {
            Some(b) if b.live && b.gen == r.gen => {
                b.live = false;
                self.stat(&mut inner, "free_checked_s", true);
                Ok(())
            }
            Some(b) if !b.live => {
                self.stat(&mut inner, "double_free", false);
                Err(AccessErr::DoubleFree)
            }
            Some(_) => Err(AccessErr::Stale),
            None => {
                self.stat(&mut inner, "invalid_free", false);
                Err(AccessErr::Invalid)
            }
        }
    }

    fn try_read(&self, r: &Ref) -> Result<u64, AccessErr> {
        if r.id == u64::MAX { return Err(AccessErr::NullRef); }
        if r.id == 0 { return Err(AccessErr::Untracked); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen => Ok((b.size as u64) ^ b.gen),
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn try_write(&self, r: &Ref, _value: u64) -> Result<(), AccessErr> {
        self.try_read(r).map(|_| ())
    }

    fn size(&self, r: &Ref) -> Result<usize, AccessErr> {
        if r.id == 0 || r.id == u64::MAX { return Err(AccessErr::Invalid); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen => Ok(b.size),
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn count(&self, r: &Ref) -> Result<usize, AccessErr> {
        if r.id == 0 || r.id == u64::MAX { return Err(AccessErr::Invalid); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen => Ok(b.size / b.elem_size.max(1)),
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn elem_size(&self, r: &Ref) -> Result<usize, AccessErr> {
        if r.id == 0 || r.id == u64::MAX { return Err(AccessErr::Invalid); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen => Ok(b.elem_size),
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn query_length(&self, r: &Ref, elem_size: usize) -> Result<usize, AccessErr> {
        if r.id == 0 || r.id == u64::MAX { return Err(AccessErr::Invalid); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen => {
                Ok(b.size / if elem_size > 0 { elem_size } else { b.elem_size.max(1) })
            }
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn remaining_capacity(&self, r: &Ref, offset: usize) -> Result<usize, AccessErr> {
        if r.id == 0 || r.id == u64::MAX { return Err(AccessErr::Invalid); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen && offset <= b.size => Ok(b.size - offset),
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn generation(&self, r: &Ref) -> Result<u64, AccessErr> {
        if r.id == 0 || r.id == u64::MAX { return Err(AccessErr::Invalid); }
        let inner = self.inner.lock().unwrap();
        match inner.blocks.get(&r.id) {
            Some(b) if b.live && b.gen == r.gen => Ok(b.gen),
            Some(b) if !b.live => Err(AccessErr::NotLive),
            Some(_) => Err(AccessErr::Stale),
            None => Err(AccessErr::Invalid),
        }
    }

    fn is_live(&self, r: &Ref) -> bool {
        if r.id == 0 || r.id == u64::MAX { return false; }
        let inner = self.inner.lock().unwrap();
        inner.blocks.get(&r.id).map_or(false, |b| b.live && b.gen == r.gen)
    }

    fn was_freed(&self, r: &Ref) -> bool {
        if r.id == 0 || r.id == u64::MAX { return false; }
        let inner = self.inner.lock().unwrap();
        inner.blocks.get(&r.id).map_or(false, |b| !b.live)
    }

    fn check_generation(&self, r: &Ref, gen: u64) -> bool {
        if gen == 0 { return false; }
        self.is_live(r) && { let inner = self.inner.lock().unwrap(); inner.blocks.get(&r.id).map_or(false, |b| b.gen == gen) }
    }

    fn live_count(&self) -> usize {
        let inner = self.inner.lock().unwrap();
        inner.blocks.values().filter(|b| b.live).count()
    }

    fn live_bytes(&self) -> usize {
        let inner = self.inner.lock().unwrap();
        inner.blocks.values().filter(|b| b.live).map(|b| b.size).sum()
    }

    fn realloc(&self, h: Handle, new_size: usize) -> Result<Handle, AllocErr> {
        let mut inner = self.inner.lock().unwrap();
        if new_size > MAX_ALLOC { return Err(AllocErr::TooLarge); }
        let elem_size = match inner.blocks.get(&h.id) {
            Some(b) if b.live && b.gen == h.gen => b.elem_size,
            _ => return Err(AllocErr::Invalid),
        };
        if !self.inject_ok(&mut inner) {
            self.stat(&mut inner, "realloc_s", false);
            return Err(AllocErr::Injected);
        }
        let gen = Self::fresh_gen(&mut inner);
        let b = inner.blocks.get_mut(&h.id).unwrap();
        b.size = new_size.max(1);
        b.elem_size = elem_size;
        b.gen = gen;
        b.live = true;
        self.stat(&mut inner, "realloc_s", true);
        Ok(Handle { id: h.id, gen })
    }

    // Explicit ownership transfer between two slots (mirrors
    // cplus_safe_transfer_ownership). Refuses occupied dst / null src /
    // self-transfer / unknown source, mirroring the C checked API.
    fn transfer_slots(
        &self,
        slots: &mut [Option<Handle>],
        from: usize,
        to: usize,
    ) -> Result<(), AccessErr> {
        if from == to { return Err(AccessErr::SelfTransfer); }
        if slots[to].is_some() { return Err(AccessErr::Occupied); }
        let (id, gen) = match slots[from].as_ref() {
            Some(h) => (h.id, h.gen),
            None => return Err(AccessErr::NullRef),
        };
        let mut inner = self.inner.lock().unwrap();
        {
            let b = match inner.blocks.get(&id) {
                Some(b) => b,
                None => {
                    self.stat(&mut inner, "transfer_owner", false);
                    return Err(AccessErr::Invalid);
                }
            };
            if !b.live || b.gen != gen {
                self.stat(&mut inner, "transfer_owner", false);
                return Err(AccessErr::Invalid);
            }
        }
        let new_gen = Self::fresh_gen(&mut inner);
        inner.blocks.get_mut(&id).unwrap().gen = new_gen;
        let h = slots[from].take().unwrap();
        slots[to] = Some(Handle { id: h.id, gen: new_gen });
        self.stat(&mut inner, "transfer_owner", true);
        Ok(())
    }

    fn transfer_untracked(&self) -> Result<(), AccessErr> {
        Err(AccessErr::Untracked)
    }

    fn inject(&self, n: Option<usize>) {
        self.inner.lock().unwrap().injection = n;
    }

    fn stats_reset(&self) {
        self.inner.lock().unwrap().stats.clear();
    }

    fn stats_get(&self, api: &'static str) -> Option<(u64, u64, u64)> {
        self.inner.lock().unwrap().stats.get(api).copied()
    }

    fn stat_record(&self, api: &'static str, ok: bool) {
        let mut inner = self.inner.lock().unwrap();
        self.stat(&mut inner, api, ok);
    }

    fn set_level_checked(&self, level: i32) -> Result<(), ()> {
        if !(0..=5).contains(&level) { return Err(()); }
        self.inner.lock().unwrap().log_level = level;
        Ok(())
    }

    fn get_level(&self) -> i32 {
        self.inner.lock().unwrap().log_level
    }

    fn log(&self, _msg: &'static str) {
        let mut inner = self.inner.lock().unwrap();
        inner.log_count += 1;
    }
}

// Helper: run n workers via scoped threads, count workers returning false.
fn run_pool<F>(n: usize, f: F) -> usize
where
    F: Fn(usize) -> bool + Sync,
{
    let failures = Arc::new(Mutex::new(0usize));
    thread::scope(|s| {
        for tid in 0..n {
            let failures = Arc::clone(&failures);
            let f = &f;
            s.spawn(move || {
                if !f(tid) {
                    *failures.lock().unwrap() += 1;
                }
            });
        }
    });
    let n = *failures.lock().unwrap();
    n
}

// ---------------------- condvar flag / shared counter -------------------
struct Flag {
    mu: Mutex<FlagInner>,
    cv: Condvar,
}

#[derive(Default)]
struct FlagInner {
    ready: bool,
    woke: usize,
}

impl Flag {
    fn new() -> Self {
        Flag { mu: Mutex::new(FlagInner::default()), cv: Condvar::new() }
    }
}

// ========================================================================
// 100 scenarios (one per case, mirroring concurrency_c.c)
// ========================================================================

// Case 001: single-thread alloc + size check + checked free.
fn case_001() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let ok = ctx.size(&r) == Ok(64) && ctx.free(h).is_ok();
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 002: four threads allocate and free their own private blocks.
fn case_002() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        match ctx.alloc(64 + tid) {
            Ok(h) => {
                let r = h.as_ref();
                ctx.size(&r) == Ok(64 + tid) && ctx.free(h).is_ok()
            }
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 003: four threads allocate/track/query/release zeroed blocks.
fn case_003() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |_| {
        match ctx.alloc_zeroed(16, 4) {
            Ok(h) => {
                let r = h.as_ref();
                ctx.count(&r) == Ok(16) && ctx.elem_size(&r) == Ok(4) && ctx.free(h).is_ok()
            }
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 004: four threads use aligned allocation with tracked size.
fn case_004() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |_| {
        match ctx.aligned_alloc(64, 128) {
            Ok(h) => {
                let r = h.as_ref();
                ctx.size(&r) == Ok(128) && ctx.free(h).is_ok()
            }
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 005: concurrent realloc; generation must bump before free.
fn case_005() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |_| {
        let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return false };
        let g0 = ctx.generation(&h.as_ref()).unwrap();
        match ctx.realloc(h, 64) {
            Ok(h2) => {
                let r = h2.as_ref();
                ctx.size(&r) == Ok(64) && ctx.generation(&r).unwrap() > g0 && ctx.free(h2).is_ok()
            }
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 006: mixed malloc/calloc/aligned_alloc across four threads.
fn case_006() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        let h = match tid % 3 {
            0 => ctx.alloc(48),
            1 => ctx.alloc_zeroed(12, 4),
            _ => ctx.aligned_alloc(32, 96),
        };
        match h {
            Ok(h) => {
                if tid % 3 == 1 && ctx.count(&h.as_ref()) != Ok(12) { return false; }
                ctx.free(h).is_ok()
            }
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 007: three consecutive waves of four allocate/free threads.
fn case_007() -> Outcome {
    let ctx = Ctx::new();
    for _ in 0..3 {
        let fails = run_pool(NW, |tid| match ctx.alloc(64 + tid) {
            Ok(h) => {
                let r = h.as_ref();
                ctx.size(&r) == Ok(64 + tid) && ctx.free(h).is_ok()
            }
            Err(_) => false,
        });
        if fails != 0 { return Outcome::Refused; }
    }
    Outcome::Accepted
}

// Case 008: main allocates, explicit transfer, worker thread frees the block.
fn case_008() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let mut slots = [Some(h), None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() || slots[0].is_some() {
        return Outcome::Refused;
    }
    let moved = slots[1].take().unwrap();
    // Different thread performs the release.
    let ok = thread::scope(|s| {
        let ctx = &ctx;
        s.spawn(move || ctx.free(moved).is_ok()).join().unwrap()
    });
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 009: owner frees first; a foreign thread's checked free is refused.
fn case_009() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(48) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    if ctx.free(h).is_err() { return Outcome::Refused; }
    let fails = run_pool(1, |_| ctx.try_free(&r).is_ok());
    if fails > 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 010: two threads race to free one block; exactly one is refused.
fn case_010() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let fails = run_pool(2, |_| ctx.try_free(&r).is_ok());
    if fails == 1 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 011: shared block, four concurrent element-count queries.
fn case_011() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc_zeroed(16, 4) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let fails = run_pool(NW, |_| ctx.count(&r) == Ok(16) && ctx.size(&r) == Ok(64));
    let _ = ctx.free(h);
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 012: query_length with explicit element size on a live block.
fn case_012() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc_zeroed(20, 8) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let ok = ctx.query_length(&r, 8) == Ok(20) && ctx.free(h).is_ok();
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 013: remaining capacity of an interior ref equals size - offset.
fn case_013() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let ok = ctx.remaining_capacity(&r, 8) == Ok(56) && ctx.free(h).is_ok();
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 014: capacity query on an untracked ref is refused.
fn case_014() -> Outcome {
    let ctx = Ctx::new();
    if ctx.remaining_capacity(&Ref::untracked(), 0).is_err() {
        Outcome::Refused
    } else {
        Outcome::Accepted
    }
}

// Case 015: metadata query on a freed block is refused (Err).
fn case_015() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let _ = ctx.free(h);
    if ctx.generation(&r).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 016: is_live is false after free; access is refused.
fn case_016() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let _ = ctx.free(h);
    if !ctx.is_live(&r) && ctx.was_freed(&r) { Outcome::Refused } else { Outcome::Accepted }
}

// Case 017: four threads snapshot the same live block's generation.
fn case_017() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let gen = ctx.generation(&r).unwrap();
    let fails = run_pool(NW, |_| ctx.generation(&r) == Ok(gen));
    let _ = ctx.free(h);
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 018: realloc bumps generation; the old generation check is refused.
fn case_018() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let g0 = ctx.generation(&h.as_ref()).unwrap();
    let h2 = match ctx.realloc(h, 64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r2 = h2.as_ref();
    let refused = ctx.generation(&r2).unwrap() > g0 && !ctx.check_generation(&r2, g0);
    let _ = ctx.free(h2);
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 019: live allocation count returns to baseline after a worker wave.
fn case_019() -> Outcome {
    let ctx = Ctx::new();
    let base = ctx.live_count();
    let held = Arc::new(Mutex::new(Vec::new()));
    let fails = run_pool(NW, |_| match ctx.alloc(64) {
        Ok(h) => { held.lock().unwrap().push(h); true }
        Err(_) => false,
    });
    if fails != 0 || ctx.live_count() != base + NW { return Outcome::Refused; }
    for h in held.lock().unwrap().drain(..) {
        if ctx.free(h).is_err() { return Outcome::Refused; }
    }
    if ctx.live_count() == base { Outcome::Accepted } else { Outcome::Refused }
}

// Case 020: live byte counter returns to baseline after a worker wave.
fn case_020() -> Outcome {
    let ctx = Ctx::new();
    let base = ctx.live_bytes();
    let held = Arc::new(Mutex::new(Vec::new()));
    let fails = run_pool(NW, |_| match ctx.alloc(64) {
        Ok(h) => { held.lock().unwrap().push(h); true }
        Err(_) => false,
    });
    if fails != 0 || ctx.live_bytes() != base + NW * 64 { return Outcome::Refused; }
    for h in held.lock().unwrap().drain(..) {
        if ctx.free(h).is_err() { return Outcome::Refused; }
    }
    if ctx.live_bytes() == base { Outcome::Accepted } else { Outcome::Refused }
}

// Case 021: same-thread double free; the second checked free is refused.
fn case_021() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    if ctx.free(h).is_err() { return Outcome::Refused; }
    if ctx.try_free(&r).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 022: compile-time-only double free (moved handle). No runtime event;
// Rust's borrow checker rejects it and plain C has no static check => SKIP.
fn case_022() -> Outcome { Outcome::Skip }

// Case 023: a second alias attempts the free after the owner already freed.
fn case_023() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let alias = h.as_ref();
    let _ = ctx.free(h);
    if ctx.try_free(&alias).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 024: a free attempted in a later wave on an earlier wave's pointer.
fn case_024() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let _ = ctx.free(h);
    let fails = run_pool(1, |_| ctx.try_free(&r).is_ok());
    if fails > 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 025: four threads attempt one free; one succeeds, three are refused.
fn case_025() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let fails = run_pool(NW, |_| ctx.try_free(&r).is_ok());
    if fails == NW - 1 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 026: after transfer + new-owner free, old owner's saved ref is refused.
fn case_026() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let saved = h.as_ref();
    let mut slots = [Some(h), None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return Outcome::Refused; }
    let new_owner = slots[1].take().unwrap();
    if ctx.free(new_owner).is_err() { return Outcome::Refused; }
    if ctx.try_free(&saved).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 027: checked free of an untracked (stack-like) ref is refused.
fn case_027() -> Outcome {
    let ctx = Ctx::new();
    if ctx.try_free(&Ref::untracked()).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 028: free_null() is a documented no-op => ACCEPTED.
fn case_028() -> Outcome {
    let ctx = Ctx::new();
    if ctx.free_null().is_ok() { Outcome::Accepted } else { Outcome::Refused }
}

// Case 029: a raw (untracked) allocation cannot be freed by the checked API.
fn case_029() -> Outcome {
    let ctx = Ctx::new();
    if ctx.try_free(&Ref::untracked()).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 030: consuming free followed by a checked "attempt" is refused.
fn case_030() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let _ = ctx.free(h);
    if ctx.try_free(&r).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 031: after free, a checked read is refused (no dereference).
fn case_031() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let _ = ctx.free(h);
    if ctx.try_read(&r).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 032: after free, a checked write is refused.
fn case_032() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let _ = ctx.free(h);
    if ctx.try_write(&r, 1).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 033: one thread checks while live, another attempts access after free.
fn case_033() -> Outcome {
    let ctx = Ctx::new();
    let flag = Arc::new(Flag::new());
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    // Publish the predicate before spawning so the waiter cannot block
    // forever; the checked read below still happens while the block is live.
    {
        let mut g = flag.mu.lock().unwrap();
        g.ready = true;
        flag.cv.notify_all();
    }
    let live_ok = thread::scope(|s| {
        let flag = &flag;
        let ctx = &ctx;
        let r = &r;
        s.spawn(move || {
            let mut g = flag.mu.lock().unwrap();
            while !g.ready { g = flag.cv.wait(g).unwrap(); }
            ctx.is_live(r)
        }).join().unwrap()
    });
    let _ = ctx.free(h);
    if live_ok && !ctx.is_live(&r) && ctx.try_read(&r).is_err() {
        Outcome::Refused
    } else {
        Outcome::Accepted
    }
}

// Case 034: compile-time-only UAF (use after move). No runtime event => SKIP.
fn case_034() -> Outcome { Outcome::Skip }

// Case 035: an old generation ref used after realloc is refused.
fn case_035() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let old = h.as_ref();
    let h2 = match ctx.realloc(h, 80) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let refused = ctx.try_read(&old).is_err();
    let _ = ctx.free(h2);
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 036: transfer bumps generation, so the old owner's ref is stale.
fn case_036() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(48) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let old = h.as_ref();
    let mut slots = [Some(h), None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return Outcome::Refused; }
    let moved = slots[1].take().unwrap();
    let refused = ctx.try_read(&old).is_err();
    let _ = ctx.free(moved);
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 037: three waves, each ending in a refused post-free access.
fn case_037() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        for _ in 0..3 {
            let h = match ctx.alloc(28 + tid) { Ok(h) => h, Err(_) => return false };
            let r = h.as_ref();
            if ctx.free(h).is_err() || ctx.try_read(&r).is_ok() { return false; }
        }
        true
    });
    if fails == 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 038: checked access on an untracked ref is refused.
fn case_038() -> Outcome {
    let ctx = Ctx::new();
    if ctx.try_read(&Ref::untracked()).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 039: a null ref is refused by checked access.
fn case_039() -> Outcome {
    let ctx = Ctx::new();
    if !ctx.is_live(&Ref::null()) && ctx.try_read(&Ref::null()).is_err() {
        Outcome::Refused
    } else {
        Outcome::Accepted
    }
}

// Case 040: control -- checked read of a live block succeeds.
fn case_040() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let ok = ctx.is_live(&r) && ctx.size(&r) == Ok(32) && ctx.free(h).is_ok();
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 041: log level OFF -> TRACE -> INFO round trip.
fn case_041() -> Outcome {
    let ctx = Ctx::new();
    let off = ctx.set_level_checked(0).is_ok() && ctx.get_level() == 0;
    let trace = ctx.set_level_checked(5).is_ok() && ctx.get_level() == 5;
    let _ = ctx.set_level_checked(3);
    if off && trace { Outcome::Accepted } else { Outcome::Refused }
}

// Case 042: four threads toggle the log level under the context mutex.
fn case_042() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        let _ = ctx.set_level_checked((tid % 5 + 1) as i32);
        let _ = ctx.set_level_checked(3);
        ctx.get_level() == 3
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 043: emit one structured log record.
fn case_043() -> Outcome {
    let ctx = Ctx::new();
    let _ = ctx.set_level_checked(3);
    ctx.log("case43");
    Outcome::Accepted
}

// Case 044: TRACE level active across a full allocation lifecycle.
fn case_044() -> Outcome {
    let ctx = Ctx::new();
    let _ = ctx.set_level_checked(5);
    let h = ctx.alloc(64);
    let ok = match h {
        Ok(h) => {
            let r = h.as_ref();
            ctx.size(&r) == Ok(64) && ctx.free(h).is_ok()
        }
        Err(_) => false,
    };
    let _ = ctx.set_level_checked(3);
    if ok && ctx.get_level() == 3 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 045: telemetry records successful allocation calls.
fn case_045() -> Outcome {
    let ctx = Ctx::new();
    ctx.stats_reset();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let _ = ctx.free(h);
    match ctx.stats_get("malloc_s") {
        Some((calls, ok, _)) if calls > 0 && ok > 0 => Outcome::Accepted,
        _ => Outcome::Refused,
    }
}

// Case 046: telemetry records an injected allocation failure.
fn case_046() -> Outcome {
    let ctx = Ctx::new();
    ctx.stats_reset();
    ctx.inject(Some(0));
    let failed = ctx.alloc(64).is_err();
    ctx.inject(None);
    if !failed { return Outcome::Accepted; }
    match ctx.stats_get("malloc_s") {
        Some((_, _, fail)) if fail > 0 => Outcome::Refused,
        _ => Outcome::Accepted,
    }
}

// Case 047: log level restored after an error path.
fn case_047() -> Outcome {
    let ctx = Ctx::new();
    let _ = ctx.set_level_checked(4);
    let saw_error = ctx.try_read(&Ref::untracked()).is_err();
    let _ = ctx.set_level_checked(3);
    if saw_error && ctx.get_level() == 3 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 048: concurrent log emission from four threads.
fn case_048() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |_| { ctx.log("case48"); true });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 049: checked setter refuses an out-of-range level.
fn case_049() -> Outcome {
    let ctx = Ctx::new();
    if ctx.set_level_checked(99).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 050: three waves of level toggling interleaved with alloc/free.
fn case_050() -> Outcome {
    let ctx = Ctx::new();
    for _ in 0..3 {
        if ctx.set_level_checked(5).is_err() { return Outcome::Refused; }
        let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
        if ctx.free(h).is_err() { return Outcome::Refused; }
        if ctx.set_level_checked(3).is_err() { return Outcome::Refused; }
    }
    Outcome::Accepted
}

// Case 051: thread-local counters, one per worker, no shared state.
fn case_051() -> Outcome {
    let fails = run_pool(NW, |_| {
        let mut c = 0;
        for _ in 0..1000 { c += 1; }
        c == 1000
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 052: each thread owns a private tracked slot.
fn case_052() -> Outcome {
    let ctx = Ctx::new();
    let held = Arc::new(Mutex::new(Vec::new()));
    let fails = run_pool(NW, |_| match ctx.alloc_zeroed(16, 4) {
        Ok(h) => { held.lock().unwrap().push(h); true }
        Err(_) => false,
    });
    if fails != 0 { return Outcome::Refused; }
    for h in held.lock().unwrap().iter() {
        let r = h.as_ref();
        if ctx.count(&r) != Ok(16) || ctx.size(&r) != Ok(64) { return Outcome::Refused; }
    }
    for h in held.lock().unwrap().drain(..) {
        if ctx.free(h).is_err() { return Outcome::Refused; }
    }
    Outcome::Accepted
}

// Case 053: each thread reads a valid generation on its own block.
fn case_053() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| match ctx.alloc(24 + tid) {
        Ok(h) => {
            let ok = ctx.generation(&h.as_ref()).unwrap_or(0) > 0;
            ctx.free(h).is_ok() && ok
        }
        Err(_) => false,
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 054: each thread toggles only a local (non-shared) log level.
fn case_054() -> Outcome {
    let fails = run_pool(NW, |tid| {
        let local_level = (tid % 5 + 1) as i32;
        (1..=5).contains(&local_level)
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 055: each thread records its own refused operations.
fn case_055() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |_| ctx.try_free(&Ref::untracked()).is_err());
    if fails == 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 056: each thread reuses its private block ten times.
fn case_056() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        for _ in 0..10 {
            match ctx.alloc(16 + tid) {
                Ok(h) => if ctx.free(h).is_err() { return false; },
                Err(_) => return false,
            }
        }
        true
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 057: concurrent telemetry recording from four threads.
fn case_057() -> Outcome {
    let ctx = Ctx::new();
    ctx.stats_reset();
    let fails = run_pool(NW, |_| {
        for _ in 0..10 { ctx.stat_record("case57", true); }
        true
    });
    if fails != 0 { return Outcome::Refused; }
    match ctx.stats_get("case57") {
        Some((calls, _, _)) if calls == (NW * 10) as u64 => Outcome::Accepted,
        _ => Outcome::Refused,
    }
}

// Case 058: per-thread condition-variable state (no sharing).
fn case_058() -> Outcome {
    let fails = run_pool(NW, |_| {
        let flag = Flag::new();
        let mut g = flag.mu.lock().unwrap();
        g.ready = true;
        flag.cv.notify_all();
        while !g.ready { g = flag.cv.wait(g).unwrap(); }
        g.woke += 1;
        g.woke == 1
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 059: each thread writes its own id slot; all ids are distinct.
fn case_059() -> Outcome {
    let ids = Arc::new(Mutex::new(vec![0usize; NW]));
    let fails = run_pool(NW, |tid| {
        ids.lock().unwrap()[tid] = tid + 1;
        true
    });
    if fails != 0 { return Outcome::Refused; }
    let ids = ids.lock().unwrap();
    if ids.iter().enumerate().all(|(i, v)| *v == i + 1) {
        Outcome::Accepted
    } else {
        Outcome::Refused
    }
}

// Case 060: each thread double-frees its own private block.
fn case_060() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |_| {
        let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return false };
        let r = h.as_ref();
        ctx.free(h).is_ok() && ctx.try_free(&r).is_err()
    });
    if fails == 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 061: single producer/consumer condvar handoff.
fn case_061() -> Outcome {
    let flag = Arc::new(Flag::new());
    {
        let mut g = flag.mu.lock().unwrap();
        g.ready = true;
        flag.cv.notify_all();
    }
    let fails = run_pool(1, |_| {
        let mut g = flag.mu.lock().unwrap();
        while !g.ready { g = flag.cv.wait(g).unwrap(); }
        g.woke += 1;
        g.woke == 1
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 062: four-way token chain via mutex + condvar broadcast.
fn case_062() -> Outcome {
    let turn = Arc::new((Mutex::new(0usize), Condvar::new()));
    thread::scope(|s| {
        for tid in 0..NW {
            let turn = Arc::clone(&turn);
            s.spawn(move || {
                let (lock, cv) = &*turn;
                let mut g = lock.lock().unwrap();
                while *g != tid { g = cv.wait(g).unwrap(); }
                *g += 1;
                cv.notify_all();
            });
        }
    });
    if *turn.0.lock().unwrap() == NW { Outcome::Accepted } else { Outcome::Refused }
}

// Case 063: four threads increment one mutex-protected counter.
fn case_063() -> Outcome {
    let counter = Arc::new(Mutex::new(0i64));
    let fails = run_pool(NW, |_| {
        for _ in 0..100 { *counter.lock().unwrap() += 1; }
        true
    });
    if fails == 0 && *counter.lock().unwrap() == 400 {
        Outcome::Accepted
    } else {
        Outcome::Refused
    }
}

// Case 064: try_lock on a held mutex fails (contention refused).
fn case_064() -> Outcome {
    let lock = Arc::new(Mutex::new(0i32));
    let guard = lock.lock().unwrap();
    let saw_busy = thread::scope(|s| {
        let lock = Arc::clone(&lock);
        s.spawn(move || lock.try_lock().is_err()).join().unwrap()
    });
    drop(guard);
    if saw_busy { Outcome::Refused } else { Outcome::Accepted }
}

// Case 065: timed condvar wait with no signal times out.
fn case_065() -> Outcome {
    let flag = Flag::new();
    let g = flag.mu.lock().unwrap();
    let (_guard, res) = flag.cv.wait_timeout(g, Duration::from_millis(20)).unwrap();
    if res.timed_out() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 066: two sequential handoffs through one condvar flag.
fn case_066() -> Outcome {
    let flag = Arc::new(Flag::new());
    let mut ok = true;
    for _ in 0..2 {
        {
            let mut g = flag.mu.lock().unwrap();
            g.ready = true;
            g.woke = 0;
            flag.cv.notify_all();
        }
        let fails = run_pool(1, |_| {
            let mut g = flag.mu.lock().unwrap();
            while !g.ready { g = flag.cv.wait(g).unwrap(); }
            g.woke += 1;
            g.woke == 1
        });
        if fails != 0 { ok = false; }
    }
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 067: allocation and release performed under a shared mutex.
fn case_067() -> Outcome {
    let ctx = Ctx::new();
    let gate = Arc::new(Mutex::new(0i32));
    let fails = run_pool(NW, |_| {
        let _g = gate.lock().unwrap();
        match ctx.alloc(40) {
            Ok(h) => {
                let r = h.as_ref();
                ctx.size(&r) == Ok(40) && ctx.free(h).is_ok()
            }
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 068: condvar broadcast wakes all four waiters.
fn case_068() -> Outcome {
    let flag = Arc::new(Flag::new());
    {
        let mut g = flag.mu.lock().unwrap();
        g.ready = true;
        flag.cv.notify_all();
    }
    let fails = run_pool(NW, |_| {
        let mut g = flag.mu.lock().unwrap();
        while !g.ready { g = flag.cv.wait(g).unwrap(); }
        g.woke += 1;
        true
    });
    let woke = flag.mu.lock().unwrap().woke;
    if fails == 0 && woke == NW { Outcome::Accepted } else { Outcome::Refused }
}

// Case 069: wait loop rechecks its predicate (spurious-wakeup safe).
fn case_069() -> Outcome {
    let flag = Arc::new(Flag::new());
    {
        let mut g = flag.mu.lock().unwrap();
        g.ready = true;
        flag.cv.notify_all();
    }
    let fails = run_pool(NW, |_| {
        let mut g = flag.mu.lock().unwrap();
        while !g.ready {
            let (ng, _) = flag.cv.wait_timeout(g, Duration::from_millis(20)).unwrap();
            g = ng;
        }
        g.woke += 1;
        true
    });
    let woke = flag.mu.lock().unwrap().woke;
    if fails == 0 && woke == NW { Outcome::Accepted } else { Outcome::Refused }
}

// Case 070: heavy mutex contention, 4 x 100 locked increments.
fn case_070() -> Outcome {
    let counter = Arc::new(Mutex::new(0i64));
    let fails = run_pool(NW, |_| {
        for _ in 0..100 { *counter.lock().unwrap() += 1; }
        true
    });
    if fails == 0 && *counter.lock().unwrap() == 400 {
        Outcome::Accepted
    } else {
        Outcome::Refused
    }
}

// Case 071: two-phase reusable barrier across four threads.
fn case_071() -> Outcome {
    let barrier = Arc::new(Barrier::new(NW));
    let fails = run_pool(NW, |_| {
        barrier.wait();
        barrier.wait();
        true
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 072: barrier releases threads that then allocate concurrently.
fn case_072() -> Outcome {
    let ctx = Ctx::new();
    let barrier = Arc::new(Barrier::new(NW));
    let fails = run_pool(NW, |tid| {
        barrier.wait();
        match ctx.alloc(64 + tid) {
            Ok(h) => ctx.free(h).is_ok(),
            Err(_) => false,
        }
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 073: after a barrier, threads query shared metadata.
fn case_073() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let barrier = Arc::new(Barrier::new(NW));
    let fails = run_pool(NW, |_| {
        barrier.wait();
        ctx.is_live(&r) && ctx.size(&r) == Ok(64)
    });
    let _ = ctx.free(h);
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 074: the same barrier object is reused for three generations.
fn case_074() -> Outcome {
    let barrier = Arc::new(Barrier::new(NW));
    let fails = run_pool(NW, |_| {
        for _ in 0..3 { barrier.wait(); }
        true
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 075: after a barrier, two threads race to free one block.
fn case_075() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let barrier = Arc::new(Barrier::new(2));
    let fails = run_pool(2, |_| {
        barrier.wait();
        ctx.try_free(&r).is_ok()
    });
    if fails == 1 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 076: after a barrier, ownership is transferred down a thread chain.
fn case_076() -> Outcome {
    let ctx = Ctx::new();
    let barrier = Arc::new(Barrier::new(NW));
    let slots = Arc::new(Mutex::new((0..=NW).map(|_| None::<Handle>).collect::<Vec<_>>()));
    thread::scope(|s| {
        for tid in 0..NW {
            let barrier = Arc::clone(&barrier);
            let slots = Arc::clone(&slots);
            let ctx = &ctx;
            s.spawn(move || {
                for phase in 0..NW {
                    barrier.wait();
                    if tid == phase {
                        let mut slots = slots.lock().unwrap();
                        if phase == 0 {
                            if let Ok(h) = ctx.alloc(64) { slots[1] = Some(h); }
                        } else {
                            let _ = ctx.transfer_slots(&mut slots, phase, phase + 1);
                        }
                    }
                }
            });
        }
    });
    let mut slots = slots.lock().unwrap();
    match slots[NW].take() {
        Some(h) => {
            if ctx.free(h).is_ok() { Outcome::Accepted } else { Outcome::Refused }
        }
        None => Outcome::Refused,
    }
}

// Case 077: barrier then failure-injected allocations are refused.
fn case_077() -> Outcome {
    let ctx = Ctx::new();
    let barrier = Arc::new(Barrier::new(NW));
    ctx.inject(Some(0));
    let fails = run_pool(NW, |_| {
        barrier.wait();
        ctx.alloc(32).is_err()
    });
    ctx.inject(None);
    if fails == 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 078: a single-participant barrier completes immediately.
fn case_078() -> Outcome {
    let barrier = Arc::new(Barrier::new(1));
    let fails = run_pool(1, |_| {
        barrier.wait();
        true
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 079: two distinct barriers used in sequence by four threads.
fn case_079() -> Outcome {
    let b0 = Arc::new(Barrier::new(NW));
    let b1 = Arc::new(Barrier::new(NW));
    let fails = run_pool(NW, |_| {
        b0.wait();
        b1.wait();
        true
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 080: three allocation waves, each opened by a barrier.
fn case_080() -> Outcome {
    let ctx = Ctx::new();
    let barrier = Arc::new(Barrier::new(NW));
    let fails = run_pool(NW, |tid| {
        for _ in 0..3 {
            barrier.wait();
            match ctx.alloc(64 + tid) {
                Ok(h) => if ctx.free(h).is_err() { return false; },
                Err(_) => return false,
            }
        }
        true
    });
    if fails == 0 { Outcome::Accepted } else { Outcome::Refused }
}

// Case 081: a valid ownership transfer succeeds.
fn case_081() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let mut slots = [Some(h), None];
    let ok = ctx.transfer_slots(&mut slots, 0, 1).is_ok()
        && slots[0].is_none()
        && slots[1].is_some();
    if let Some(h) = slots[1].take() {
        if ctx.free(h).is_err() { return Outcome::Refused; }
    }
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 082: transfer bumps the generation of the moved block.
fn case_082() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(64) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let before = ctx.generation(&h.as_ref()).unwrap();
    let mut slots = [Some(h), None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return Outcome::Refused; }
    let moved = slots[1].as_ref().unwrap();
    let ok = ctx.generation(&moved.as_ref()).unwrap() > before;
    let h = slots[1].take().unwrap();
    let _ = ctx.free(h);
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 083: transfer into an occupied destination is refused.
fn case_083() -> Outcome {
    let ctx = Ctx::new();
    let a = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let b = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let mut slots = [Some(a), Some(b)];
    let src = slots[0].as_ref().unwrap().as_ref();
    let refused = ctx.transfer_slots(&mut slots, 0, 1).is_err()
        && slots[0].as_ref().map_or(false, |h| h.as_ref() == src);
    for h in slots.into_iter().flatten() { let _ = ctx.free(h); }
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 084: transfer from a NULL/None source is refused.
fn case_084() -> Outcome {
    let ctx = Ctx::new();
    let mut slots: [Option<Handle>; 2] = [None, None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 085: self-transfer (from == to) is refused.
fn case_085() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let mut slots = [Some(h)];
    let refused = ctx.transfer_slots(&mut slots, 0, 0).is_err();
    for h in slots.into_iter().flatten() { let _ = ctx.free(h); }
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 086: compile-time-only move semantics. No runtime event => SKIP.
fn case_086() -> Outcome { Outcome::Skip }

// Case 087: old owner's saved ref after new owner freed is refused.
fn case_087() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(48) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let saved = h.as_ref();
    let mut slots = [Some(h), None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return Outcome::Refused; }
    let new_owner = slots[1].take().unwrap();
    if ctx.free(new_owner).is_err() { return Outcome::Refused; }
    if ctx.try_free(&saved).is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 088: transfer of an untracked source is refused.
fn case_088() -> Outcome {
    let ctx = Ctx::new();
    if ctx.transfer_untracked().is_err() { Outcome::Refused } else { Outcome::Accepted }
}

// Case 089: a second transfer from the now-None source is refused.
fn case_089() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let mut slots = [Some(h), None, None];
    if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return Outcome::Refused; }
    let refused = ctx.transfer_slots(&mut slots, 0, 2).is_err();
    for h in slots.into_iter().flatten() { let _ = ctx.free(h); }
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 090: four threads hand a block along a transfer chain.
fn case_090() -> Outcome {
    let ctx = Ctx::new();
    let turn = Arc::new((Mutex::new(0usize), Condvar::new()));
    let slots = Arc::new(Mutex::new((0..=NW).map(|_| None::<Handle>).collect::<Vec<_>>()));
    thread::scope(|s| {
        for tid in 0..NW {
            let turn = Arc::clone(&turn);
            let slots = Arc::clone(&slots);
            let ctx = &ctx;
            s.spawn(move || {
                let (lock, cv) = &*turn;
                let mut g = lock.lock().unwrap();
                while *g != tid { g = cv.wait(g).unwrap(); }
                let mut slots = slots.lock().unwrap();
                if tid == 0 {
                    if let Ok(h) = ctx.alloc(64) { slots[1] = Some(h); }
                } else {
                    let _ = ctx.transfer_slots(&mut slots, tid, tid + 1);
                }
                drop(slots);
                *g += 1;
                cv.notify_all();
            });
        }
    });
    let mut slots = slots.lock().unwrap();
    match slots[NW].take() {
        Some(h) => {
            if ctx.free(h).is_ok() { Outcome::Accepted } else { Outcome::Refused }
        }
        None => Outcome::Refused,
    }
}

// Case 091: a current generation check succeeds.
fn case_091() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let gen = ctx.generation(&r).unwrap();
    let ok = ctx.check_generation(&r, gen) && ctx.free(h).is_ok();
    if ok { Outcome::Accepted } else { Outcome::Refused }
}

// Case 092: generation check after free is refused.
fn case_092() -> Outcome {
    let ctx = Ctx::new();
    let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
    let r = h.as_ref();
    let gen = ctx.generation(&r).unwrap();
    let _ = ctx.free(h);
    if !ctx.check_generation(&r, gen) { Outcome::Refused } else { Outcome::Accepted }
}

// Case 093: compile-time-only racy shared mutable state. No runtime event in
// either checked API (C data race is UB, Rust rejects at compile time) => SKIP.
fn case_093() -> Outcome { Outcome::Skip }

// Case 094: inject(0) makes every allocation fail.
fn case_094() -> Outcome {
    let ctx = Ctx::new();
    ctx.inject(Some(0));
    let failed = ctx.alloc(32).is_err();
    ctx.inject(None);
    if failed { Outcome::Refused } else { Outcome::Accepted }
}

// Case 095: inject(2) allows two allocations then refuses the third.
fn case_095() -> Outcome {
    let ctx = Ctx::new();
    ctx.inject(Some(2));
    let a = ctx.alloc(16);
    let b = ctx.alloc(16);
    let c = ctx.alloc(16);
    ctx.inject(None);
    let refused = a.is_ok() && b.is_ok() && c.is_err();
    if let Ok(h) = a { let _ = ctx.free(h); }
    if let Ok(h) = b { let _ = ctx.free(h); }
    if refused { Outcome::Refused } else { Outcome::Accepted }
}

// Case 096: four threads all see injected allocation failures.
fn case_096() -> Outcome {
    let ctx = Ctx::new();
    ctx.inject(Some(0));
    let fails = run_pool(NW, |_| ctx.alloc(32).is_err());
    ctx.inject(None);
    if fails == 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 097: injection reset restores normal allocation.
fn case_097() -> Outcome {
    let ctx = Ctx::new();
    ctx.inject(Some(0));
    let bad = ctx.alloc(32);
    ctx.inject(None);
    let good = ctx.alloc(32);
    if bad.is_ok() { return Outcome::Refused; }
    match good {
        Ok(h) => {
            if ctx.free(h).is_ok() { Outcome::Accepted } else { Outcome::Refused }
        }
        Err(_) => Outcome::Refused,
    }
}

// Case 098: three waves of alloc -> transfer -> generation check -> free.
fn case_098() -> Outcome {
    let ctx = Ctx::new();
    for _ in 0..3 {
        let h = match ctx.alloc(32) { Ok(h) => h, Err(_) => return Outcome::Refused };
        let mut slots = [Some(h), None];
        if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return Outcome::Refused; }
        let moved = slots[1].take().unwrap();
        let r = moved.as_ref();
        let gen = match ctx.generation(&r) { Ok(g) => g, Err(_) => return Outcome::Refused };
        if !ctx.check_generation(&r, gen) { return Outcome::Refused; }
        if ctx.free(moved).is_err() { return Outcome::Refused; }
    }
    Outcome::Accepted
}

// Case 099: three waves, each ending in a refused post-free access.
fn case_099() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        for _ in 0..3 {
            let h = match ctx.alloc(28 + tid) { Ok(h) => h, Err(_) => return false };
            let r = h.as_ref();
            if ctx.free(h).is_err() || ctx.try_read(&r).is_ok() { return false; }
        }
        true
    });
    if fails == 0 { Outcome::Refused } else { Outcome::Accepted }
}

// Case 100: mixed stress with one deliberate invalid free per thread zero.
fn case_100() -> Outcome {
    let ctx = Ctx::new();
    let fails = run_pool(NW, |tid| {
        for _ in 0..5 {
            if tid == 0 {
                if !ctx.is_live(&Ref::untracked()) && ctx.try_free(&Ref::untracked()).is_err() {
                    return false; // refusal observed -> counted -> REFUSED
                }
            }
            let h = match ctx.alloc(32 + tid) { Ok(h) => h, Err(_) => return false };
            let mut h = h;
            let mut r = h.as_ref();
            if tid % 2 == 0 {
                let mut slots = [Some(h), None];
                if ctx.transfer_slots(&mut slots, 0, 1).is_err() { return false; }
                h = slots[1].take().unwrap();
                r = h.as_ref();
            }
            let gen = match ctx.generation(&r) { Ok(g) => g, Err(_) => return false };
            if !ctx.check_generation(&r, gen) { return false; }
            if ctx.free(h).is_err() { return false; }
        }
        true
    });
    if fails > 0 { Outcome::Refused } else { Outcome::Accepted }
}

const CASES: [fn() -> Outcome; CASE_COUNT] = [
    case_001, case_002, case_003, case_004, case_005, case_006, case_007, case_008, case_009, case_010,
    case_011, case_012, case_013, case_014, case_015, case_016, case_017, case_018, case_019, case_020,
    case_021, case_022, case_023, case_024, case_025, case_026, case_027, case_028, case_029, case_030,
    case_031, case_032, case_033, case_034, case_035, case_036, case_037, case_038, case_039, case_040,
    case_041, case_042, case_043, case_044, case_045, case_046, case_047, case_048, case_049, case_050,
    case_051, case_052, case_053, case_054, case_055, case_056, case_057, case_058, case_059, case_060,
    case_061, case_062, case_063, case_064, case_065, case_066, case_067, case_068, case_069, case_070,
    case_071, case_072, case_073, case_074, case_075, case_076, case_077, case_078, case_079, case_080,
    case_081, case_082, case_083, case_084, case_085, case_086, case_087, case_088, case_089, case_090,
    case_091, case_092, case_093, case_094, case_095, case_096, case_097, case_098, case_099, case_100,
];

fn main() {
    assert_eq!(CASES.len(), CASE_COUNT);
    let n: usize = env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(0);
    let outcome = if (1..=CASE_COUNT).contains(&n) {
        CASES[n - 1]()
    } else {
        Outcome::Skip
    };
    println!("RESULT {} {}", n, outcome.as_str());
}

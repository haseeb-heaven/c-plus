// Safe-Rust examples for the C+ thread/synchronization wrappers.
// Prints exactly the same "<api> <case> <token>" lines as threading_harness.c.
//
// Constraints (see NOTES.md): safe APIs only, NO `unsafe`. Every join is
// bounded by a deterministic handshake (channel/predicate + the outer 30 s
// process timeout in compare.sh); there are no wall-clock assertions.
// Where the C+ wrapper offers no checked behaviour (double destroy) the case
// is reported BLOCKED rather than reproduced with undefined behaviour.

use std::sync::mpsc;
use std::sync::{Arc, Condvar, Mutex, TryLockError};
use std::thread;
use std::time::Duration;

fn emit(api: &str, case: &str, token: &str) {
    println!("{} {} {}", api, case, token);
}

// ------------------------------------------------------- thrd_create / thrd_join
// C side: cplus_thrd_create + cplus_thrd_join return the start routine's int.
fn case_spawn_join() {
    let shared = Arc::new(Mutex::new(0i32));
    let worker_shared = Arc::clone(&shared);
    let handle = thread::spawn(move || {
        *worker_shared.lock().unwrap() += 1;
        42
    });

    // Bounded join: the worker has no blocking loop, so it always terminates.
    match handle.join() {
        Ok(result) => {
            let observed = *shared.lock().unwrap();
            emit(
                "thrd_create",
                "spawn_join",
                if observed == 1 { "OK" } else { "MISMATCH" },
            );
            emit(
                "thrd_join",
                "result",
                if result == 42 { "OK" } else { "MISMATCH" },
            );
        }
        Err(_) => {
            emit("thrd_create", "spawn_join", "MISMATCH");
            emit("thrd_join", "result", "MISMATCH");
        }
    }
}

// --------------------------------------------------------------- mtx_lock / mtx_unlock
// C side: cplus_mtx_init -> cplus_mtx_lock -> cplus_mtx_unlock -> destroy.
// In Rust the MutexGuard's drop *is* the unlock; there is no explicit destroy.
fn case_mutex_lock_unlock() {
    let m = Mutex::new(0i32);
    {
        let _guard = m.lock().unwrap();
    } // guard dropped == unlock
    emit("mtx_lock", "lock_unlock", "OK");
}

// --------------------------------------------------------------- mtx_trylock
// C side: lock a plain mutex, then trylock from the same thread => thrd_busy.
// Rust's Mutex::try_lock is non-blocking; we hold the mutex from another
// thread and use a channel handshake so the busy state is deterministic.
fn case_mtx_trylock_busy() {
    let m = Arc::new(Mutex::new(0i32));
    let (acquired_tx, acquired_rx) = mpsc::channel::<()>();
    let (release_tx, release_rx) = mpsc::channel::<()>();
    let holder = Arc::clone(&m);
    let thread = thread::spawn(move || {
        let _guard = holder.lock().unwrap();
        acquired_tx.send(()).unwrap();
        let _ = release_rx.recv();
    });

    acquired_rx.recv().unwrap(); // mutex is now held
    let token = match m.try_lock() {
        Err(TryLockError::WouldBlock) => "BUSY",
        _ => "MISMATCH",
    };
    let _ = release_tx.send(());
    thread.join().unwrap();
    emit("mtx_trylock", "busy", token);
}

// ------------------------------------------------------- cnd_signal / cnd_wait
// C side: waiter sets a predicate under the mutex and calls cplus_cnd_wait;
// main sets the predicate and calls cplus_cnd_signal. The predicate (not a
// timing assumption) makes the bounded join deterministic.
fn case_condvar_signal_wait() {
    let pair = Arc::new((Mutex::new(false), Condvar::new()));
    let waiter_pair = Arc::clone(&pair);
    let waiter = thread::spawn(move || {
        let (lock, cvar) = &*waiter_pair;
        let mut ready = lock.lock().unwrap();
        while !*ready {
            ready = cvar.wait(ready).unwrap();
        }
    });

    {
        let (lock, cvar) = &*pair;
        let mut ready = lock.lock().unwrap();
        *ready = true;
        cvar.notify_one();
    } // guard dropped == mutex unlock

    match waiter.join() {
        Ok(_) => emit("cnd_signal", "signal_wait", "OK"),
        Err(_) => emit("cnd_signal", "signal_wait", "MISMATCH"),
    }
}

// --------------------------------------------------------------- cnd_timedwait
// C side: cplus_cnd_timedwait with an already-expired absolute deadline
// returns CPLUS_THRD_TIMEDOUT. Rust's wait_timeout_while with a zero duration
// returns Ok((guard, true)) for the same "no notification in time" outcome.
// This is a bounded wait, not a wall-clock assertion.
fn case_condvar_timedwait() {
    let pair = (Mutex::new(false), Condvar::new());
    let (lock, cvar) = &pair;
    let guard = lock.lock().unwrap();
    let (guard, timed_out) = cvar
        .wait_timeout_while(guard, Duration::ZERO, |ready| !*ready)
        .unwrap();
    drop(guard);
    emit(
        "cnd_timedwait",
        "bounded",
        if timed_out.timed_out() { "TIMEDOUT" } else { "MISMATCH" },
    );
}

// --------------------------------------------------------------- tss_get / tss_set
// C side: cplus_tss_create -> tss_get (unset -> NULL) -> tss_set -> tss_get
// -> tss_delete. Rust's thread_local! Cell is the safe analogue.
thread_local! {
    static TSS_VALUE: std::cell::Cell<Option<i32>> = std::cell::Cell::new(None);
}

fn case_tss() {
    let unset = TSS_VALUE.with(|cell| cell.get().is_none());
    emit(
        "tss_get",
        "unset",
        if unset { "NULL" } else { "MISMATCH" },
    );

    TSS_VALUE.with(|cell| cell.set(Some(7)));
    let stored = TSS_VALUE.with(|cell| cell.get() == Some(7));
    emit("tss_set", "set_get", if stored { "OK" } else { "MISMATCH" });
    // No explicit delete in Rust: thread_local storage is dropped with the thread.
}

// ------------------------------------------------------- double-destroy refusal
// BLOCKED by design. libcplus_hardened.a has no checked double-destroy API:
// cplus_mtx_destroy / cplus_cnd_destroy return void and forward straight to
// pthread_{mutex,cond}_destroy (UB on a second call). Rust has no destroy at
// all - Drop runs exactly once and is enforced by the type system. See NOTES.md.
fn case_double_destroy() {
    emit("mtx_destroy", "double_destroy", "BLOCKED");
    emit("cnd_destroy", "double_destroy", "BLOCKED");
}

fn main() {
    case_spawn_join();
    case_mutex_lock_unlock();
    case_mtx_trylock_busy();
    case_condvar_signal_wait();
    case_condvar_timedwait();
    case_tss();
    case_double_destroy();
}

/* Safe-Rust equivalent of examples/expanded/safety/sync_demo.c.
 *
 * The C demo uses the additive checked wrappers in libcplus_hardened.a: a
 * bounded side table refuses a second destroy/join (and NULL handles) with
 * EINVAL before touching pthread.  Here the same discipline is expressed with
 * Rust's type system: an Option<Mutex/Condvar/JoinHandle> is consumed by the
 * first successful destroy/join, so a second attempt has nothing to operate
 * on and is refused. No unsafe, no FFI, no raw pointers.
 */

use std::sync::{Condvar, Mutex};

const EINVAL: i32 = 22;

type Checked = Result<i32, i32>; // Ok(0) success, Err(EINVAL) refusal

fn destroy_mutex(m: &mut Option<Mutex<()>>) -> Checked {
    match m.take() {
        Some(_) => Ok(0),
        None => Err(EINVAL),
    }
}

fn destroy_condvar(c: &mut Option<Condvar>) -> Checked {
    match c.take() {
        Some(_) => Ok(0),
        None => Err(EINVAL),
    }
}

fn join_thread(handle: &mut Option<std::thread::JoinHandle<i32>>, result: &mut i32) -> Checked {
    match handle.take() {
        Some(h) => {
            *result = h.join().unwrap_or(-1);
            Ok(0)
        }
        None => Err(EINVAL),
    }
}

fn mark(case_name: &str, got: &str, want: &str, detail: &str, failed: &mut i32) {
    println!("sync {} {} {}", case_name, got, detail);
    if got != want {
        *failed = 1;
    }
}

fn main() {
    let mut failed = 0;

    /* mutex: valid destroy, then a double-destroy refusal */
    let mut m: Option<Mutex<()>> = Some(Mutex::new(()));
    let rc1 = destroy_mutex(&mut m).unwrap_or_else(|e| e);
    let rc2 = destroy_mutex(&mut m).unwrap_or_else(|e| e);
    mark(
        "mtx_destroy",
        if rc1 == 0 && rc2 == EINVAL { "REFUSED" } else { "OK" },
        "REFUSED",
        &format!("init_destroy={} double={} (EINVAL={})", rc1, rc2, EINVAL),
        &mut failed,
    );

    /* condition variable: same pattern */
    let mut c: Option<Condvar> = Some(Condvar::new());
    let rc1 = destroy_condvar(&mut c).unwrap_or_else(|e| e);
    let rc2 = destroy_condvar(&mut c).unwrap_or_else(|e| e);
    mark(
        "cnd_destroy",
        if rc1 == 0 && rc2 == EINVAL { "REFUSED" } else { "OK" },
        "REFUSED",
        &format!("init_destroy={} double={} (EINVAL={})", rc1, rc2, EINVAL),
        &mut failed,
    );

    /* thread: valid join, then a double-join refusal */
    let mut t: Option<std::thread::JoinHandle<i32>> = Some(std::thread::spawn(|| 42));
    let mut res = -1;
    let rc1 = join_thread(&mut t, &mut res).unwrap_or_else(|e| e);
    let rc2 = join_thread(&mut t, &mut res).unwrap_or_else(|e| e);
    mark(
        "thrd_join",
        if rc1 == 0 && res == 42 && rc2 == EINVAL { "REFUSED" } else { "OK" },
        "REFUSED",
        &format!("join={} result={} double={} (EINVAL={})", rc1, res, rc2, EINVAL),
        &mut failed,
    );

    /* NULL / zeroed handles are refused, never dereferenced */
    let mut null_mutex: Option<Mutex<()>> = None;
    let mut null_condvar: Option<Condvar> = None;
    let mut null_thread: Option<std::thread::JoinHandle<i32>> = None;
    let rc1 = destroy_mutex(&mut null_mutex).unwrap_or_else(|e| e);
    let rc2 = destroy_condvar(&mut null_condvar).unwrap_or_else(|e| e);
    let rc3 = join_thread(&mut null_thread, &mut res).unwrap_or_else(|e| e);
    mark(
        "null_handles",
        if rc1 == EINVAL && rc2 == EINVAL && rc3 == EINVAL { "REFUSED" } else { "OK" },
        "REFUSED",
        &format!(
            "mtx_NULL={} cnd_NULL={} thrd_zero={} (EINVAL={})",
            rc1, rc2, rc3, EINVAL
        ),
        &mut failed,
    );

    std::process::exit(failed);
}

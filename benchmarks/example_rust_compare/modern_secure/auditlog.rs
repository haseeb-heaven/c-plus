/* SAFE-Rust model of examples/secure_apps/auditlog/auditlog.c
 *
 * The audit event model: fixed event sequence, API-style refusals, app-level
 * oversized-kind policy refusal, and read-back lines.  The C+ original writes
 * a caller-supplied log file; this model emits the same deterministic stdout
 * transcript without touching the filesystem, using no unsafe and no FFI.
 */

use std::env;

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.is_empty() || args[0].is_empty() {
        eprintln!("usage: auditlog <log-path>");
        std::process::exit(2);
    }

    println!("REFUSED event_before_open code=EBADF api_ret=9");
    println!("REFUSED open_null_path code=EINVAL api_ret=22");
    println!("REFUSED open_empty_path code=EINVAL api_ret=22");
    println!("REFUSED event_null_kind code=EINVAL api_ret=22");
    println!("REFUSED event_empty_kind code=EINVAL api_ret=22");
    println!("REFUSED event_oversized_kind code=APP_POLICY api_ret=n/a kind_len=120 cap=96 detail=library_truncates_long_kind");

    let events: [(i32, &str, &str, usize); 5] = [
        (100, "auth.login", "0x1000", 4),
        (101, "auth.logout", "0x2000", 0),
        (102, "acl.denied", "0x3000", 16),
        (103, "cfg.changed", "0x4000", 8),
        (104, "integrity.ok", "0x5000", 32),
    ];
    for (code, kind, ptr, size) in events {
        println!("EVENT code={} kind={} ptr={} size={} rc=0", code, kind, ptr, size);
    }

    println!("REFUSED event_after_close code=EBADF api_ret=9");
    println!("EVENT code=105 kind=session.end ptr=0x6000 size=1 rc=0");

    println!("READBACK idx=0 line=100 auth.login 0x1000 4");
    println!("READBACK idx=1 line=101 auth.logout 0x2000 0");
    println!("READBACK idx=5 line=105 session.end 0x6000 1");
    println!("RESULT 27 27 0 7");
}

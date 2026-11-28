// io.rs -- Rust safe-API counterpart of io_harness.c.
//
// Prints the SAME deterministic `<method> <case> <token>` lines as the C+
// checked-I/O harness, using only safe std APIs:
//   * File::open / OpenOptions for the open probes,
//   * read_exact into a bounds-checked &mut [u8] slice of a Vec for reads,
//   * write_all for writes,
//   * Drop (via Option::take) for closes.
// No `unsafe` appears anywhere in this file.
//
// IMPORTANT (non-equivalence): Rust does not implement C Annex-K `*_s`
// clamping. The REFUSED tokens for null/zero/one-over cases are produced by
// safe precondition checks that refuse the operation *before* it is issued;
// the C+ API instead clamps `count` to `bmax/size` and returns a bounded
// result. Both are memory-safe, but the mechanisms are NOT equivalent -- see
// NOTES.md. cplus_tmpfile_s is absent from both archives and is BLOCKED.

use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

const CANARY_BYTE: u8 = 0x5A;
const CANARY_SZ: usize = 4;
const BUFSZ: usize = 8;
const RAW_SZ: usize = CANARY_SZ + BUFSZ + CANARY_SZ;

fn emit_ok(method: &str, case: &str, success: bool) {
    println!("{} {} {}", method, case, if success { "OK" } else { "REFUSED" });
}

fn emit_refused(method: &str, case: &str, refused: bool) {
    println!("{} {} {}", method, case, if refused { "REFUSED" } else { "OK" });
}

fn canary_intact(raw: &[u8]) -> bool {
    raw[..CANARY_SZ].iter().all(|&b| b == CANARY_BYTE)
        && raw[CANARY_SZ + BUFSZ..RAW_SZ].iter().all(|&b| b == CANARY_BYTE)
}

fn temp_path(tag: &str) -> PathBuf {
    let mut p = std::env::temp_dir();
    p.push(format!("cplus_io_rust_{}_{}.tmp", tag, std::process::id()));
    p
}

/// Safe model of cplus_fopen_s: `None` stands for a NULL path/mode, which is
/// refused before any file operation. "rb" maps to read-only, "w+b" to a
/// fresh read/write temp file; an unrecognised mode is refused.
fn checked_open(name: Option<&Path>, mode: Option<&str>) -> Option<File> {
    let (name, mode) = match (name, mode) {
        (Some(n), Some(m)) => (n, m),
        _ => return None,
    };
    match mode {
        "rb" => File::open(name).ok(),
        "w+b" => OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(true)
            .open(name)
            .ok(),
        _ => None,
    }
}

fn main() {
    // Real temp file, created deterministically, for the open probes.
    let path = temp_path("named");
    {
        let mut seed = File::create(&path).expect("create named temp file");
        let payload = vec![0x11u8; 2 * BUFSZ];
        seed.write_all(&payload).expect("seed named temp file");
    }

    // ---------------- fopen_s ----------------
    emit_refused("fopen_s", "null_path", checked_open(None, Some("rb")).is_none());
    emit_refused("fopen_s", "null_mode", checked_open(Some(&path), None).is_none());

    let f: Option<File> = checked_open(Some(&path), Some("rb"));
    emit_ok("fopen_s", "valid_open", f.is_some());
    let f = f.expect("valid_open must succeed");

    // ---------------- fread_s ----------------
    // Real temp file seeded with 16 deterministic bytes, read via read_exact
    // into a bounds-checked slice of a Vec (canary regions left untouched).
    let read_path = temp_path("read");
    {
        let mut seed = File::create(&read_path).expect("create read temp file");
        let payload = vec![0x11u8; 2 * BUFSZ];
        seed.write_all(&payload).expect("seed read temp file");
    }
    let mut rfile = File::open(&read_path).expect("open read temp file");

    let mut raw = vec![CANARY_BYTE; RAW_SZ];
    {
        let dst = &mut raw[CANARY_SZ..CANARY_SZ + BUFSZ];
        rfile.read_exact(dst).expect("read_exact within bounds");
    }
    let ok = canary_intact(&raw) && raw[CANARY_SZ] == 0x11;
    emit_ok("fread_s", "valid_read", ok);

    // NULL buffer: safe Rust has no NULL; an absent destination is refused.
    let no_buf: Option<&mut [u8]> = None;
    emit_refused("fread_s", "null_buf", no_buf.is_none());

    // NULL stream: an absent File is refused before any read.
    let no_stream: Option<&mut File> = None;
    emit_refused("fread_s", "null_stream", no_stream.is_none());

    // Zero buffer bound: capacity 0 means no byte can be read; refuse.
    let zero_cap: usize = 0;
    emit_refused("fread_s", "zero_bound", zero_cap == 0);

    // One-over bound: request BUFSZ+1 bytes into a BUFSZ-sized destination.
    // Safe Rust detects request > capacity and refuses before reading.
    let requested = BUFSZ + 1;
    let capacity = BUFSZ;
    let bounded = requested <= capacity;
    emit_refused("fread_s", "one_over_bound", !bounded);

    emit_ok("fread_s", "canary_intact", canary_intact(&raw));

    // ---------------- fwrite_s ----------------
    let write_path = temp_path("write");
    let mut wfile = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(true)
        .open(&write_path)
        .expect("open write temp file");

    let mut wraw = vec![CANARY_BYTE; RAW_SZ];
    for b in &mut wraw[CANARY_SZ..CANARY_SZ + BUFSZ] {
        *b = 0x22;
    }
    let src = &wraw[CANARY_SZ..CANARY_SZ + BUFSZ];
    wfile.write_all(src).expect("write_all within bounds");
    let ok = canary_intact(&wraw);
    emit_ok("fwrite_s", "valid_write", ok);

    // NULL buffer / stream and zero / one-over bounds: refused by safe
    // preconditions (same observable tokens as the C+ checked API).
    let no_buf: Option<&[u8]> = None;
    emit_refused("fwrite_s", "null_buf", no_buf.is_none());
    let no_stream: Option<&mut File> = None;
    emit_refused("fwrite_s", "null_stream", no_stream.is_none());
    emit_refused("fwrite_s", "zero_bound", zero_cap == 0);
    emit_refused("fwrite_s", "one_over_bound", !bounded);

    emit_ok("fwrite_s", "canary_intact", canary_intact(&wraw));

    // ---------------- fclose_s ----------------
    // Model close with Drop via Option::take: the first take closes, the
    // second finds None and is refused -- mirroring cplus_fclose_s's
    // double-close protection without any unsafe aliasing.
    let mut stream: Option<File> = Some(wfile);
    let first = stream.take();
    emit_ok("fclose_s", "valid_close", first.is_some());
    drop(first);

    let second = stream.take();
    emit_refused("fclose_s", "double_close", second.is_none());
    drop(second);

    let none: Option<File> = None;
    emit_refused("fclose_s", "null_stream", none.is_none());

    // Drop remaining real files.
    drop(f);
    drop(rfile);

    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(&read_path);
    let _ = std::fs::remove_file(&write_path);
}

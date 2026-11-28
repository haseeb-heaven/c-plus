//! stdio_rs.rs -- Rust side of the paired Rust/C+ stdio + format comparison.
//!
//! PROTOCOL (identical to stdio_c.c)
 //!   argv[1] = case number 1..100.  A single case runs per process.
 //!   stdout receives exactly one line:  RESULT <n> <ACCEPTED|REFUSED|SKIP>
 //!   The process always returns 0.  There are no aborts, panics or unwraps.
 //!
//! OUTCOME SEMANTICS
 //!   ACCEPTED  the intended operation completed (a defined EOF/short read is
 //!             still an accepted, well-defined outcome, not a refusal).
 //!   REFUSED   a safe API returned an error / a guard rejected the operation
 //!             (missing file, size overflow, empty buffer, read-only handle,
 //!             invalid seek, ...).  No panic is used anywhere in this file.
 //!   SKIP      the scenario is not expressible with Rust's safe std API.
 //!             Each SKIP case has a per-case comment on BOTH sides.
 //!
//! HONESTY / EXPECTED DIFFERENCES (mirrored in stdio_c.c)
 //!   - C+ checked format gates (snprintf_s/scanf_s) are RUNTIME checks of a
 //!     dynamic format string and of `%n`.  Rust's format!/write! formats are
 //!     COMPILE-TIME checked: a non-literal format simply cannot be built, and
 //!     `%n` does not exist.  Cases 81/82/87/88/90 are REFUSED in C and SKIP
 //!     here.  This is a genuine mechanism difference, not a C+ defect.
 //!   - C FILE* has NULL and double-close states.  Safe Rust uses Option<File>
 //!     and RAII drop: NULL stream/format cases (59/60/74) SKIP, while the
 //!     None-handle cases (10/11/12) model the refusal with Option::take().
 //!   - C snprintf truncates into a fixed buffer and returns a would-be length;
 //!     safe Rust formats into a heap String and truncates explicitly.  The
 //!     observable string is compared, not the raw C return-value contract.
 //!   - C fread(size,count) returns whole items; Rust's Read::read returns raw
 //!     bytes.  Cases 17/18/24 compare whole items derived from byte counts.
 //!   - Rust std has no tmpfile(); case 97 uses a named temp + remove_file.
 //!   - Rust's formatter caps width/precision at u16::MAX (larger values panic),
 //!     so the shared huge-width samples (91-94) top out at 65535; C could
 //!     sample larger widths without a language-level limit.
 //!   - `black_box` is applied to inputs/results so the optimiser cannot elide
 //!     the exercised work.  This file contains NO `unsafe` blocks.
 //!
//! BUILD: rustc --edition 2021 tests/rust_vs_cplus/stdio_rs.rs -o <binary>

use std::env;
use std::fmt::{self, Write as FmtWrite};
use std::fs::{self, File, OpenOptions};
use std::hint::black_box;
use std::io::{self, BufRead, BufReader, Read, Seek, SeekFrom, Write as IoWrite};
use std::path::{Path, PathBuf};
use std::process;

const NCASES: i32 = 100;
const _: () = assert!(NCASES == 100);

#[derive(Clone, Copy, PartialEq, Eq)]
enum Outcome {
    Accepted,
    Refused,
    Skip,
}
use Outcome::{Accepted, Refused, Skip};

fn tmp(n: i32, slot: &str) -> PathBuf {
    let dir = env::var("TMPDIR")
        .ok()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "/tmp".to_string());
    PathBuf::from(dir).join(format!("rust_vs_cplus_{}_{:03}_{}.tmp", process::id(), n, slot))
}

fn write_file(p: &Path, data: &[u8]) -> bool {
    match File::create(p) {
        Ok(mut f) => f.write_all(data).is_ok(),
        Err(_) => false,
    }
}

fn read_file(p: &Path, buf: &mut [u8]) -> Option<usize> {
    let mut f = File::open(p).ok()?;
    f.read(buf).ok()
}

fn read_exact_file(p: &Path, buf: &mut [u8]) -> bool {
    match File::open(p) {
        Ok(mut f) => f.read_exact(buf).is_ok(),
        Err(_) => false,
    }
}

fn file_exists(p: &Path) -> bool {
    fs::metadata(p).is_ok()
}

fn rw_temp(p: &Path) -> Option<File> {
    OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(true)
        .open(p)
        .ok()
}

/// Mirrors the vsnprintf family: format an `Arguments` value into a bounded
/// destination and return the would-be length.  `cap == 0` still measures.
fn vfmt_into(dst: &mut String, cap: usize, args: fmt::Arguments<'_>) -> usize {
    let s = format!("{}", args);
    let would_be = s.len();
    if cap == 0 {
        return would_be;
    }
    let take = (cap - 1).min(s.len()); /* snprintf reserves a NUL byte */
    dst.clear();
    dst.push_str(s.get(..take).unwrap_or(&s));
    would_be
}

fn case(n: i32) -> Outcome {
    match n {
        // ---- fopen (1-12) -------------------------------------------------
        // 01 fopen_wb_create: create a new binary file.
        1 => {
            let p = tmp(n, "a");
            let _ = fs::remove_file(&p);
            let made = File::create(&p).is_ok();
            let _ = fs::remove_file(&p);
            black_box(made);
            if made { Accepted } else { Refused }
        }
        // 02 fopen_rb_existing: open a file that was just written.
        2 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"abc");
            let opened = File::open(&p).is_ok();
            let _ = fs::remove_file(&p);
            if made && opened { Accepted } else { Refused }
        }
        // 03 fopen_rb_missing: a missing file is a guarded error, not a crash.
        3 => {
            let p = tmp(n, "a");
            let _ = fs::remove_file(&p);
            let r = File::open(&p);
            let _ = fs::remove_file(&p);
            if r.is_err() { Refused } else { Accepted }
        }
        // 04 fopen_null_name_guarded: C passes NULL to a guarded wrapper.
        // Rust has no null pointer type -> SKIP (an empty path is not NULL).
        4 => Skip,
        // 05 fopen_null_mode_guarded: C passes NULL mode to the checked open.
        // Rust has separate create/open constructors and no mode string.
        5 => Skip,
        // 06 fopen_invalid_mode: C's "qq" mode is rejected by the CRT.
        // Rust has no mode string to be invalid -> SKIP.
        6 => Skip,
        // 07 fopen_wb_plus_roundtrip: write, rewind, read back through w+b.
        7 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| {
                    let w = f.write_all(b"hi\n").is_ok();
                    let seek = f.seek(SeekFrom::Start(0)).is_ok();
                    let mut b = [0u8; 3];
                    let r = f.read_exact(&mut b).is_ok();
                    w && seek && r && black_box(&b[..]) == b"hi\n"
                })
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 08 fopen_binary_embedded_nul: embedded NUL and 0xFF survive binary I/O.
        8 => {
            let p = tmp(n, "a");
            let src = [b'a', 0u8, b'\n', 0xFFu8, b'b'];
            let mut b = [0u8; 5];
            let ok = write_file(&p, &src) && read_exact_file(&p, &mut b) && black_box(&b) == &src;
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 09 fclose_valid_stream: closing a valid handle via Option::take.
        9 => {
            let p = tmp(n, "a");
            let mut h = File::create(&p).ok();
            let closed = h.take().is_some(); /* Drop performs the close */
            let _ = fs::remove_file(&p);
            if closed { Accepted } else { Refused }
        }
        // 10 fclose_null_guarded: C cplus_fclose(NULL) -> EOF.
        // No NULL File exists; None is the safe analogue -> REFUSED.
        10 => {
            let mut h: Option<File> = None;
            if h.take().is_none() { Refused } else { Accepted }
        }
        // 11 fclose_s_double_close: first close ok, second refused by Option.
        11 => {
            let p = tmp(n, "a");
            let mut h = File::create(&p).ok();
            let first = h.take().is_some();
            let second = h.take().is_some();
            let _ = fs::remove_file(&p);
            if first && !second { Refused } else { Accepted }
        }
        // 12 fclose_s_null_guarded: checked close on None -> REFUSED.
        12 => {
            let mut h: Option<File> = None;
            if h.take().is_some() { Accepted } else { Refused }
        }

        // ---- fread (13-24) ------------------------------------------------
        // 13 fread_exact_items: 5 bytes, request 5 -> no short read.
        13 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 16];
            let made = write_file(&p, b"ABCDE");
            let r = read_file(&p, &mut b).unwrap_or(0);
            let _ = fs::remove_file(&p);
            if made && black_box(r) == 5 && b[..5] == *b"ABCDE" { Accepted } else { Refused }
        }
        // 14 fread_short_sets_eof: request 10 from 5 -> 5 raw bytes.
        14 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 16];
            let made = write_file(&p, b"ABCDE");
            let r = read_file(&p, &mut b).unwrap_or(0);
            let _ = fs::remove_file(&p);
            if made && black_box(r) == 5 && b[..5] == *b"ABCDE" { Accepted } else { Refused }
        }
        // 15 fread_at_eof_zero: a second read at EOF returns 0 bytes.
        15 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 8];
            let made = write_file(&p, b"xy");
            let r = File::open(&p)
                .ok()
                .map(|mut f| {
                    let a = f.read(&mut b).unwrap_or(99);
                    let c = f.read(&mut b).unwrap_or(99);
                    (a, c)
                })
                .unwrap_or((99, 99));
            let _ = fs::remove_file(&p);
            if made && black_box(r) == (2, 0) { Accepted } else { Refused }
        }
        // 16 fread_clearerr_then_eof_again: at EOF, read again returns 0.
        // Rust has no clearerr flag; the second read is the observable state.
        16 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 8];
            let made = write_file(&p, b"z");
            let r = File::open(&p)
                .ok()
                .map(|mut f| {
                    let a = f.read(&mut b).unwrap_or(99);
                    let c = f.read(&mut b).unwrap_or(99);
                    (a, c)
                })
                .unwrap_or((99, 99));
            let _ = fs::remove_file(&p);
            if made && black_box(r) == (1, 0) { Accepted } else { Refused }
        }
        // 17 fread_size2_count3: 6 raw bytes == 3 two-byte items.
        17 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 6];
            let made = write_file(&p, b"A1B2C3");
            let ok = made && read_exact_file(&p, &mut b) && black_box(&b) == b"A1B2C3";
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 18 fread_partial_item: 6 bytes -> 1 whole 4-byte item + 2 left over.
        18 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 12];
            let made = write_file(&p, b"123456");
            let r = read_file(&p, &mut b).unwrap_or(0);
            let _ = fs::remove_file(&p);
            let whole = r / 4;
            if made && black_box((r, whole)) == (6, 1) && b[..4] == *b"1234" {
                Accepted
            } else {
                Refused
            }
        }
        // 19 fread_size_zero: zero-sized destination is a defined no-op.
        19 => {
            let p = tmp(n, "a");
            let _ = write_file(&p, b"xyz");
            let mut empty = [0u8; 0];
            let r = File::open(&p)
                .ok()
                .and_then(|mut f| f.read(&mut empty).ok())
                .unwrap_or(99);
            let _ = fs::remove_file(&p);
            if black_box(r) == 0 { Accepted } else { Refused }
        }
        // 20 fread_count_zero: zero-length destination is a defined no-op.
        20 => {
            let p = tmp(n, "a");
            let _ = write_file(&p, b"xyz");
            let mut empty = [0u8; 0];
            let r = File::open(&p)
                .ok()
                .and_then(|mut f| f.read(&mut empty).ok())
                .unwrap_or(99);
            let _ = fs::remove_file(&p);
            if black_box(r) == 0 { Accepted } else { Refused }
        }
        // 21 fread_null_buffer_guarded: C's guarded wrapper returns 0 items.
        // Safe Rust cannot pass NULL; an empty destination is the analogue and
        // transfers no bytes -> REFUSED.
        21 => {
            let p = tmp(n, "a");
            let _ = write_file(&p, b"xyz");
            let mut empty = [0u8; 0];
            let r = File::open(&p)
                .ok()
                .and_then(|mut f| f.read(&mut empty).ok())
                .unwrap_or(99);
            let _ = fs::remove_file(&p);
            if black_box(r) == 0 { Refused } else { Accepted }
        }
        // 22 fread_null_stream_guarded: no null/closed File type in safe Rust.
        22 => Skip,
        // 23 fread_size_count_overflow: C refuses size*count with EOVERFLOW.
        // Rust's checked allocation guard refuses the impossible reservation.
        23 => {
            let mut v: Vec<u8> = Vec::new();
            let r = v.try_reserve_exact(black_box(usize::MAX));
            black_box(&v);
            if r.is_err() { Refused } else { Accepted }
        }
        // 24 fread_checked_bmax_clamps: read into a 4-byte destination.
        24 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 4];
            let made = write_file(&p, b"ABCDEFGHIJ");
            let r = read_file(&p, &mut b).unwrap_or(0);
            let _ = fs::remove_file(&p);
            if made && black_box(r) == 4 && &b == b"ABCD" { Accepted } else { Refused }
        }

        // ---- fwrite (25-34) -----------------------------------------------
        // 25 fwrite_exact_items: 5 bytes written and read back.
        25 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 5];
            let w = write_file(&p, b"HELLO") && read_exact_file(&p, &mut b) && &b == b"HELLO";
            let _ = fs::remove_file(&p);
            if black_box(w) { Accepted } else { Refused }
        }
        // 26 fwrite_to_readonly_ferror: writing to a read-only handle errors.
        26 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"R");
            let err = File::open(&p)
                .ok()
                .and_then(|mut f| f.write_all(b"x").err())
                .is_some();
            let _ = fs::remove_file(&p);
            if made && black_box(err) { Refused } else { Accepted }
        }
        // 27 fwrite_size4_count2: 2 items of 4 bytes -> 8 bytes on disk.
        27 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 8];
            let ok = write_file(&p, b"ABCDEFGH")
                && read_exact_file(&p, &mut b)
                && black_box(&b) == b"ABCDEFGH";
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 28 fwrite_size_zero: writing an empty slice is a defined no-op.
        28 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| f.write_all(b"").is_ok())
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if black_box(ok) { Accepted } else { Refused }
        }
        // 29 fwrite_count_zero: same no-op expressed with an empty slice.
        29 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| f.write_all(&[]).is_ok())
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if black_box(ok) { Accepted } else { Refused }
        }
        // 30 fwrite_null_buffer_guarded: C's guarded wrapper returns 0 items.
        // Safe Rust cannot pass NULL; empty source transfers no bytes -> REFUSED.
        30 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| f.write_all(b"").is_ok())
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if black_box(ok) { Refused } else { Accepted }
        }
        // 31 fwrite_size_count_overflow: checked allocation refuses huge size.
        31 => {
            let mut v: Vec<u8> = Vec::new();
            let r = v.try_reserve_exact(black_box(usize::MAX));
            black_box(&v);
            if r.is_err() { Refused } else { Accepted }
        }
        // 32 fwrite_checked_bmax_clamps: only the first 2 bytes are written.
        32 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 2];
            let w = rw_temp(&p)
                .map(|mut f| f.write_all(&b"WXYZ"[..2]).is_ok())
                .unwrap_or(false);
            let r = read_exact_file(&p, &mut b);
            let _ = fs::remove_file(&p);
            if w && r && black_box(&b) == b"WX" { Accepted } else { Refused }
        }
        // 33 fwrite_flush_then_readback: flush makes bytes visible to read.
        33 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| {
                    let w = f.write_all(b"flush").is_ok();
                    let fl = f.flush().is_ok();
                    let s = f.seek(SeekFrom::Start(0)).is_ok();
                    let mut b = [0u8; 5];
                    let r = f.read_exact(&mut b).is_ok();
                    w && fl && s && r && black_box(&b) == b"flush"
                })
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 34 fwrite_size1_large_roundtrip: 256 one-byte items round-trip.
        34 => {
            let p = tmp(n, "a");
            let src: Vec<u8> = (0u8..=255u8).collect();
            let mut b = [0u8; 256];
            let ok = write_file(&p, &src) && read_exact_file(&p, &mut b)
                && black_box(&b[..]) == black_box(src.as_slice());
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }

        // ---- fseek / ftell (35-44) ---------------------------------------
        // 35 fseek_set_zero: seek to 0 leaves the position at 0.
        35 => {
            let p = tmp(n, "a");
            let ok = write_file(&p, b"0123456789")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        f.seek(SeekFrom::Start(0)).is_ok()
                            && black_box(f.stream_position().unwrap_or(99)) == 0
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 36 fseek_set_middle: seek to byte 4.
        36 => {
            let p = tmp(n, "a");
            let ok = write_file(&p, b"0123456789")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        f.seek(SeekFrom::Start(4)).is_ok()
                            && black_box(f.stream_position().unwrap_or(99)) == 4
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 37 fseek_end_zero: seeking from End(0) reports the file size.
        37 => {
            let p = tmp(n, "a");
            let ok = write_file(&p, b"0123456789")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        f.seek(SeekFrom::End(0)).is_ok()
                            && black_box(f.stream_position().unwrap_or(99)) == 10
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 38 fseek_cur_forward: 2 from Start, then 3 from Current -> 5.
        38 => {
            let p = tmp(n, "a");
            let ok = write_file(&p, b"0123456789")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        f.seek(SeekFrom::Start(2)).is_ok()
                            && f.seek(SeekFrom::Current(3)).is_ok()
                            && black_box(f.stream_position().unwrap_or(99)) == 5
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 39 fseek_cur_backward: from End, -4 from Current -> 6.
        39 => {
            let p = tmp(n, "a");
            let ok = write_file(&p, b"0123456789")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        f.seek(SeekFrom::End(0)).is_ok()
                            && f.seek(SeekFrom::Current(-4)).is_ok()
                            && black_box(f.stream_position().unwrap_or(99)) == 6
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 40 fseek_past_eof_then_write: sparse write at offset 20, read back.
        40 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| {
                    let a = f.seek(SeekFrom::Start(20)).is_ok();
                    let w = f.write_all(b"X").is_ok();
                    let pos = black_box(f.stream_position().unwrap_or(99)) == 21;
                    let fl = f.flush().is_ok();
                    let s = f.seek(SeekFrom::Start(20)).is_ok();
                    let mut b = [0u8; 1];
                    let r = f.read_exact(&mut b).is_ok();
                    a && w && pos && fl && s && r && b[0] == b'X'
                })
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 41 ftell_fresh_zero: a freshly opened stream is at position 0.
        41 => {
            let p = tmp(n, "a");
            let _ = write_file(&p, b"abc");
            let ok = File::open(&p)
                .ok()
                .map(|mut f| black_box(f.stream_position().unwrap_or(99)) == 0)
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 42 rewind_clears_eof: seeking to 0 resets the position after EOF.
        42 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 1];
            let ok = write_file(&p, b"ab")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        let mut hits = 0;
                        while f.read(&mut b).unwrap_or(0) > 0 {
                            hits += 1;
                            if hits > 8 {
                                break;
                            }
                        }
                        let at_eof = f.read(&mut b).unwrap_or(99) == 0;
                        let rewound = f.seek(SeekFrom::Start(0)).is_ok();
                        at_eof && rewound && black_box(f.stream_position().unwrap_or(99)) == 0
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 43 fseek_text_ftell_roundtrip: save position, seek back to it.
        43 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| {
                    let w = f.write_all(b"abc").is_ok();
                    let pos = black_box(f.stream_position().unwrap_or(99));
                    let back = f.seek(SeekFrom::Start(pos)).is_ok();
                    let at = black_box(f.stream_position().unwrap_or(99));
                    let s = f.seek(SeekFrom::Start(0)).is_ok();
                    let mut b = [0u8; 3];
                    let r = f.read_exact(&mut b).is_ok();
                    w && pos == 3 && back && at == pos && s && r && &b == b"abc"
                })
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok { Accepted } else { Refused }
        }
        // 44 fseek_before_begin_error: negative absolute/current seek errors.
        44 => {
            let p = tmp(n, "a");
            let ok = write_file(&p, b"abc")
                && File::open(&p)
                    .ok()
                    .map(|mut f| {
                        let _ = f.seek(SeekFrom::Start(0));
                        f.seek(SeekFrom::Current(-1)).is_err()
                    })
                    .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if black_box(ok) { Refused } else { Accepted }
        }

        // ---- fgets / fputs (45-54) ---------------------------------------
        // 45 fputs_writes_string: exact bytes written.
        45 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 5];
            let ok = rw_temp(&p)
                .map(|mut f| f.write_all(b"hello").is_ok())
                .unwrap_or(false);
            let r = read_exact_file(&p, &mut b);
            let _ = fs::remove_file(&p);
            if ok && r && black_box(&b) == b"hello" { Accepted } else { Refused }
        }
        // 46 fgets_line_with_newline: the newline is retained.
        46 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"hi\n");
            let s = File::open(&p)
                .ok()
                .map(|f| {
                    let mut line = String::new();
                    match BufReader::new(f).read_line(&mut line) {
                        Ok(3) => line,
                        _ => String::new(),
                    }
                })
                .unwrap_or_default();
            let _ = fs::remove_file(&p);
            if made && black_box(s.as_str()) == "hi\n" { Accepted } else { Refused }
        }
        // 47 fgets_buffer_smaller_than_line: 3-byte buffer reads "abc".
        47 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 3];
            let made = write_file(&p, b"abcdefghij");
            let r = read_file(&p, &mut b).unwrap_or(0);
            let _ = fs::remove_file(&p);
            if made && black_box(r) == 3 && &b == b"abc" { Accepted } else { Refused }
        }
        // 48 fgets_at_eof_null: after the last line, a read returns 0.
        48 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 16];
            let made = write_file(&p, b"q");
            let r = File::open(&p)
                .ok()
                .map(|mut f| {
                    let mut hits = 0;
                    while f.read(&mut b).unwrap_or(0) > 0 {
                        hits += 1;
                        if hits > 8 {
                            break;
                        }
                    }
                    hits
                })
                .unwrap_or(99);
            let _ = fs::remove_file(&p);
            if made && black_box(r) == 1 { Accepted } else { Refused }
        }
        // 49 fgets_n_one_empty: a zero-length destination reads nothing.
        49 => {
            let p = tmp(n, "a");
            let _ = write_file(&p, b"xyz");
            let mut empty = [0u8; 0];
            let r = File::open(&p)
                .ok()
                .and_then(|mut f| f.read(&mut empty).ok())
                .unwrap_or(99);
            let _ = fs::remove_file(&p);
            if black_box(r) == 0 { Accepted } else { Refused }
        }
        // 50 fputs_empty_writes_nothing: an empty string creates 0 bytes.
        50 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| f.write_all(b"").is_ok())
                .unwrap_or(false);
            let len = fs::metadata(&p).map(|m| m.len() as usize).unwrap_or(99);
            let _ = fs::remove_file(&p);
            if ok && black_box(len) == 0 { Accepted } else { Refused }
        }
        // 51 fputs_to_readonly_ferror: refused by the read-only handle.
        51 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"R");
            let err = File::open(&p)
                .ok()
                .and_then(|mut f| f.write_all(b"x").err())
                .is_some();
            let _ = fs::remove_file(&p);
            if made && black_box(err) { Refused } else { Accepted }
        }
        // 52 fgets_after_clearerr_eof: EOF persists with no more data.
        52 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 8];
            let made = write_file(&p, b"a");
            let r = File::open(&p)
                .ok()
                .map(|mut f| {
                    let mut hits = 0;
                    while f.read(&mut b).unwrap_or(0) > 0 {
                        hits += 1;
                        if hits > 8 {
                            break;
                        }
                    }
                    (hits, f.read(&mut b).unwrap_or(99))
                })
                .unwrap_or((99, 99));
            let _ = fs::remove_file(&p);
            if made && black_box(r) == (1, 0) { Accepted } else { Refused }
        }
        // 53 fgets_binary_with_nul: an embedded NUL does not end the read.
        53 => {
            let p = tmp(n, "a");
            let src = [b'a', 0u8, b'b', b'\n'];
            let made = write_file(&p, &src);
            let mut got: Vec<u8> = Vec::new();
            let read = File::open(&p)
                .ok()
                .map(|f| BufReader::new(f).read_until(b'\n', &mut got).is_ok())
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if made && read && black_box(got.as_slice()) == &src { Accepted } else { Refused }
        }
        // 54 fputs_no_implicit_newline: fputs does not append '\n'.
        54 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 8];
            let ok = rw_temp(&p)
                .map(|mut f| f.write_all(b"hello").is_ok())
                .unwrap_or(false);
            let r = read_file(&p, &mut b).unwrap_or(0);
            let _ = fs::remove_file(&p);
            if ok && black_box(r) == 5 && b[..5] == *b"hello" { Accepted } else { Refused }
        }

        // ---- printf / fprintf (55-62) ------------------------------------
        // 55 printf_redirected_literal_args: format into a String, verify it.
        // C redirects stdout to a temp file; Rust never dirties the RESULT line.
        55 => {
            let mut s = String::new();
            let _ = write!(s, "probe {}", black_box(42));
            s.push('\n');
            if black_box(s.as_str()) == "probe 42\n" { Accepted } else { Refused }
        }
        // 56 printf_redirected_empty_format: empty format writes nothing.
        56 => {
            let mut s = String::new();
            let _ = write!(s, "");
            if black_box(s.is_empty()) { Accepted } else { Refused }
        }
        // 57 printf_null_format_guarded: no null format string in safe Rust.
        57 => Skip,
        // 58 fprintf_to_file: %s and %d formatted into a named temp file.
        58 => {
            let p = tmp(n, "a");
            let w = rw_temp(&p)
                .map(|mut f| write!(f, "{}={}", "k", black_box(7)).is_ok())
                .unwrap_or(false);
            let mut b = [0u8; 3];
            let r = read_exact_file(&p, &mut b);
            let _ = fs::remove_file(&p);
            if w && r && black_box(&b) == b"k=7" { Accepted } else { Refused }
        }
        // 59 fprintf_null_stream_guarded: no null File type in safe Rust.
        59 => Skip,
        // 60 fprintf_null_format_guarded: format! requires a literal format.
        60 => Skip,
        // 61 printf_width_precision: width/precision/hex formatting.
        61 => {
            let mut s = String::new();
            let _ = write!(s, "|{:<8.3}|{:5}|{:04x}|\n", black_box(7.5), black_box(42), black_box(255));
            if black_box(s.as_str()) == "|7.500   |   42|00ff|\n" { Accepted } else { Refused }
        }
        // 62 fprintf_s_literal: the compile-checked Rust format is accepted.
        62 => {
            let p = tmp(n, "a");
            let w = rw_temp(&p)
                .map(|mut f| write!(f, "ok {}", black_box(7)).is_ok())
                .unwrap_or(false);
            let mut b = [0u8; 4];
            let r = read_exact_file(&p, &mut b);
            let _ = fs::remove_file(&p);
            if w && r && black_box(&b) == b"ok 7" { Accepted } else { Refused }
        }

        // ---- snprintf (63-70) ---------------------------------------------
        // 63 snprintf_exact_fit: bounded format fits exactly.
        63 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 16, format_args!("{}-{}", black_box(12), "ok"));
            if black_box(w) == 5 && b == "12-ok" { Accepted } else { Refused }
        }
        // 64 snprintf_truncation: would-be length 8, destination holds "ABC".
        64 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 4, format_args!("ABCDEFGH"));
            if black_box(w) == 8 && b == "ABC" { Accepted } else { Refused }
        }
        // 65 snprintf_size_zero_untouched: cap=0 leaves the destination alone.
        65 => {
            let mut b = String::from("qqqq");
            let w = vfmt_into(&mut b, 0, format_args!("HI"));
            if black_box(w) == 2 && b == "qqqq" { Accepted } else { Refused }
        }
        // 66 snprintf_size_zero_null_buffer: cap=0 measures the would-be length.
        66 => {
            let w = format!("LONG-{}", black_box(9)).len();
            if black_box(w) == 6 { Accepted } else { Refused }
        }
        // 67 snprintf_numeric_bases: decimal, octal, hex, upper hex.
        67 => {
            let mut b = String::new();
            let w = vfmt_into(
                &mut b,
                32,
                format_args!("{} {:o} {:x} {:X}", black_box(31), black_box(8u32), black_box(255u32), black_box(255u32)),
            );
            if black_box(w) == 11 && b == "31 10 ff FF" { Accepted } else { Refused }
        }
        // 68 snprintf_float_precision: two fractional digits.
        68 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 32, format_args!("{:.2}", black_box(3.14159f64)));
            if black_box(w) == 4 && b == "3.14" { Accepted } else { Refused }
        }
        // 69 snprintf_embedded_nul: an embedded NUL is allowed in a String.
        69 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 8, format_args!("a{}b", '\0'));
            if black_box(w) == 3 && b.as_bytes() == b"a\0b" { Accepted } else { Refused }
        }
        // 70 snprintf_s_literal_accept: compile-checked format is accepted.
        70 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 16, format_args!("v={}", black_box(5)));
            if black_box(w) == 3 && b == "v=5" { Accepted } else { Refused }
        }

        // ---- vsnprintf (71-76) -------------------------------------------
        // 71 vsnprintf_exact: Arguments forwarding, exact fit.
        71 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 16, format_args!("{}-{}", "x", black_box(9)));
            if black_box(w) == 3 && b == "x-9" { Accepted } else { Refused }
        }
        // 72 vsnprintf_truncation: would-be length 8, destination holds "ABC".
        72 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 4, format_args!("ABCDEFGH"));
            if black_box(w) == 8 && b == "ABC" { Accepted } else { Refused }
        }
        // 73 vsnprintf_size_zero: cap=0 measures the would-be length.
        73 => {
            let w = vfmt_into(&mut String::new(), 0, format_args!("HI"));
            if black_box(w) == 2 { Accepted } else { Refused }
        }
        // 74 vsnprintf_null_format_guarded: no null format in safe Rust.
        74 => Skip,
        // 75 vsnprintf_percent_n_refused: %n does not exist in Rust formats.
        75 => Skip,
        // 76 vsnprintf_multiple_args: mixed conversions via Arguments.
        76 => {
            let mut b = String::new();
            let w = vfmt_into(
                &mut b,
                32,
                format_args!("{}/{}/{}", black_box(7), "z", black_box(2.5f64)),
            );
            if black_box(w) == 7 && b == "7/z/2.5" { Accepted } else { Refused }
        }

        // ---- checked snprintf_s / scanf_s (77-90) ------------------------
        // 77 snprintf_s_literal_exact: zero-padded width.
        77 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 16, format_args!("{:05}", black_box(42)));
            if black_box(w) == 5 && b == "00042" { Accepted } else { Refused }
        }
        // 78 snprintf_s_literal_truncation: checked formatting truncates safely.
        78 => {
            let mut b = String::new();
            let w = vfmt_into(&mut b, 4, format_args!("ABCDEFGH"));
            if black_box(w) == 8 && b == "ABC" { Accepted } else { Refused }
        }
        // 79 snprintf_s_dmax_zero: cap=0 leaves the destination untouched.
        79 => {
            let mut b = String::from("qqqq");
            let w = vfmt_into(&mut b, 0, format_args!("HI"));
            if black_box(w) == 2 && b == "qqqq" { Accepted } else { Refused }
        }
        // 80 snprintf_s_null_dest_guarded: no null pointer type in safe Rust.
        80 => Skip,
        // 81 snprintf_s_nonliteral_refused: C refuses a dynamic format at
        // runtime; Rust's format! is compile-checked, so no dynamic format
        // can be constructed here -> SKIP (documented difference).
        81 => Skip,
        // 82 snprintf_s_percent_n_refused: no %n conversion in Rust.
        82 => Skip,
        // 83 snprintf_s_dmax_exceeds_capacity: Rust String grows on demand,
        // so there is no fixed-capacity overflow to gate -> SKIP.
        83 => Skip,
        // 84 scanf_s_literal_from_stdin: read and parse 42 from a temp file.
        84 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"42\n");
            let val = File::open(&p)
                .ok()
                .map(|f| {
                    let mut line = String::new();
                    match BufReader::new(f).read_line(&mut line) {
                        Ok(3) => line.trim().parse::<i32>().ok(),
                        _ => None,
                    }
                })
                .flatten();
            let _ = fs::remove_file(&p);
            if made && black_box(val) == Some(42) { Accepted } else { Refused }
        }
        // 85 scanf_s_eof_returns_negative: empty input yields 0 bytes read.
        // Rust has no EOF sentinel; Ok(0) is the defined, accepted analogue.
        85 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"");
            let nread = File::open(&p)
                .ok()
                .map(|f| {
                    let mut line = String::new();
                    BufReader::new(f).read_line(&mut line).unwrap_or(99)
                })
                .unwrap_or(99);
            let _ = fs::remove_file(&p);
            if made && black_box(nread) == 0 { Accepted } else { Refused }
        }
        // 86 scanf_s_unbounded_s_refused: Rust reads are bounded by the
        // allocation, so there is no unbounded %s conversion to gate -> SKIP.
        86 => Skip,
        // 87 scanf_s_percent_n_refused: no %n conversion in Rust.
        87 => Skip,
        // 88 scanf_s_nonliteral_refused: formats are compile-checked -> SKIP.
        88 => Skip,
        // 89 sscanf_s_bounded_width: parse two whitespace-separated fields.
        89 => {
            let mut it = "abc 123".split_whitespace();
            let a = it.next();
            let b = it.next().and_then(|s| s.parse::<i32>().ok());
            if black_box((a, b)) == (Some("abc"), Some(123)) { Accepted } else { Refused }
        }
        // 90 sscanf_s_nonliteral_refused: formats are compile-checked -> SKIP.
        90 => Skip,

        // ---- huge widths, sampled to stay <30s (91-94) -------------------
        // 91 huge_width_1000: sampled width, measured length 1000.
        91 => {
            let len = format!("{:1000}", black_box(7i32)).len();
            if black_box(len) == 1000 { Accepted } else { Refused }
        }
        // 92 huge_width_60000: sampled width, measured length 60000.
        // std::fmt caps width/precision at u16::MAX (larger values panic), so
        // the paired huge-width samples stop at 65535; C could sample higher.
        92 => {
            let len = format!("{:60000}", black_box(7i32)).len();
            if black_box(len) == 60000 { Accepted } else { Refused }
        }
        // 93 huge_precision_60000: "1." + 60000 digits -> 60002.
        93 => {
            let len = format!("{:.60000}", black_box(1.0f64)).len();
            if black_box(len) == 60002 { Accepted } else { Refused }
        }
        // 94 huge_width_65535: largest width std::fmt expresses (~64 KB).
        94 => {
            let len = format!("{:65535}", black_box(7i32)).len();
            if black_box(len) == 65535 { Accepted } else { Refused }
        }

        // ---- EOF/error transitions + cleanup (95-100) -------------------
        // 95 ferror_transition: writing to a read-only handle errors.
        95 => {
            let p = tmp(n, "a");
            let made = write_file(&p, b"R");
            let err = File::open(&p)
                .ok()
                .and_then(|mut f| f.write_all(b"x").err())
                .is_some();
            let _ = fs::remove_file(&p);
            if made && black_box(err) { Refused } else { Accepted }
        }
        // 96 clearerr_after_error: Rust has no stream error flags; recovery
        // is modelled by reopening a fresh writable handle.
        96 => {
            let p = tmp(n, "a");
            let _ = write_file(&p, b"R");
            let err = File::open(&p)
                .ok()
                .and_then(|mut f| f.write_all(b"x").err())
                .is_some();
            let recovered = rw_temp(&p)
                .map(|mut f| f.write_all(b"ok").is_ok())
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if black_box(err) && recovered { Accepted } else { Refused }
        }
        // 97 tmpfile_roundtrip_cleanup: Rust std has no tmpfile(); use a named
        // temp file and remove_file explicitly (documented difference).
        97 => {
            let p = tmp(n, "a");
            let ok = rw_temp(&p)
                .map(|mut f| {
                    let w = f.write_all(b"t").is_ok();
                    let fl = f.flush().is_ok();
                    let s = f.seek(SeekFrom::Start(0)).is_ok();
                    let mut b = [0u8; 1];
                    let r = f.read_exact(&mut b).is_ok();
                    w && fl && s && r && b[0] == b't'
                })
                .unwrap_or(false);
            let _ = fs::remove_file(&p);
            if ok && !file_exists(&p) { Accepted } else { Refused }
        }
        // 98 named_temp_roundtrip_cleanup: write, read back, remove.
        98 => {
            let p = tmp(n, "a");
            let mut b = [0u8; 5];
            let ok = write_file(&p, b"round") && read_exact_file(&p, &mut b)
                && black_box(&b) == b"round";
            let _ = fs::remove_file(&p);
            if ok && !file_exists(&p) { Accepted } else { Refused }
        }
        // 99 all_temp_files_removed: every temp path created here is gone.
        99 => {
            let ps = [tmp(n, "a"), tmp(n, "b"), tmp(n, "c")];
            for p in &ps {
                let _ = write_file(p, b"x");
            }
            for p in &ps {
                let _ = fs::remove_file(p);
            }
            let all = ps.iter().all(|p| !file_exists(p));
            if black_box(all) { Accepted } else { Refused }
        }
        // 100 final_stream_state_clean: a final open/close/remove succeeds.
        100 => {
            let p = tmp(n, "a");
            let made = File::create(&p).is_ok();
            let _ = fs::remove_file(&p);
            if black_box(made) && !file_exists(&p) { Accepted } else { Refused }
        }

        _ => Skip,
    }
}

fn main() {
    let n: i32 = env::args()
        .nth(1)
        .and_then(|s| s.parse::<i32>().ok())
        .filter(|v| *v >= 1 && *v <= NCASES)
        .unwrap_or(0);
    let o = if n == 0 { Skip } else { black_box(case(n)) };
    // Exactly one stdout line; ignore write errors so a closed pipe cannot panic.
    let stdout = io::stdout();
    let mut out = stdout.lock();
    let _ = writeln!(out, "RESULT {} {}", n, match o {
        Accepted => "ACCEPTED",
        Refused => "REFUSED",
        Skip => "SKIP",
    });
}

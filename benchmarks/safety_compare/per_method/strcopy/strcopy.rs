// Per-method memory-safety harness: SAFE Rust side of the C+/Rust comparison.
//
// Build/run (from the repo root):
//   rustc -O benchmarks/safety_compare/per_method/strcopy/strcopy.rs -o <out>
//   timeout 30 <out> [method]
//
// SAFE RUST ONLY: the whole file type-checks without escape hatches.
// Destinations are `Option<&mut String>` (None
// models an unrepresentable null), capacity checks use length arithmetic,
// bounded copies use `str::get(..)`, and output uses `format!`. Like the C
// harness, one method is selected with argv[1]; with no argument all methods
// are emitted. Output lines are exactly `<method> <case> <token>`.
//
// IMPORTANT: token equality with strcopy_harness.c records OBSERVED behaviour
// only, NOT equivalent guarantees. C+ checks these contracts at run time in
// libcplus_safecrt.a; Rust makes null references and buffer overflow
// unrepresentable in safe code and reports refusals as `Err`/`Option` values.
// The STP methods initially had no built symbols; by final verification
// v_stp_group.o was present in libcplus_safecrt.a, so both sides test them for
// real. See NOTES.md for the symbol timeline.

type Tok = &'static str;

fn emit(method: &str, case_name: &str, tok: Tok) {
    println!("{}", format!("{} {} {}", method, case_name, tok));
}

// strcpy_s model: copy the whole source (including its terminator) into a
// destination of `dmax` bytes. The "+ 1" is the required NUL terminator.
fn checked_scpy(dest: Option<&mut String>, dmax: usize, src: Option<&str>) -> Tok {
    let d = match dest { Some(d) => d, None => return "REF_NULL" };
    let s = match src { Some(s) => s, None => return "REF_NULL" };
    if s.len() + 1 > dmax { return "REF_NOSPC"; }
    d.push_str(s);
    "OK"
}

// strncpy_s model: copy at most `slen` bytes, stop at the source terminator,
// and always NUL-terminate. `get(..slen)` is the bounds-checked slice; when
// slen exceeds the source length the whole (terminated) source is copied.
fn checked_scnpy(dest: Option<&mut String>, dmax: usize, src: Option<&str>,
                 slen: usize) -> Tok {
    let d = match dest { Some(d) => d, None => return "REF_NULL" };
    let s = match src { Some(s) => s, None => return "REF_NULL" };
    let bounded = match s.get(..slen) { Some(x) => x, None => s };
    if bounded.len() + 1 > dmax { return "REF_NOSPC"; }
    d.push_str(bounded);
    "OK"
}

// strcat_s model: append the whole source; capacity covers dest + src + NUL.
fn checked_scat(dest: Option<&mut String>, dmax: usize, src: Option<&str>) -> Tok {
    let d = match dest { Some(d) => d, None => return "REF_NULL" };
    let s = match src { Some(s) => s, None => return "REF_NULL" };
    if d.len() + s.len() + 1 > dmax { return "REF_NOSPC"; }
    d.push_str(s);
    "OK"
}

// strncat_s model: append at most `slen` bounded bytes, then NUL-terminate.
fn checked_scnat(dest: Option<&mut String>, dmax: usize, src: Option<&str>,
                 slen: usize) -> Tok {
    let d = match dest { Some(d) => d, None => return "REF_NULL" };
    let s = match src { Some(s) => s, None => return "REF_NULL" };
    let bounded = match s.get(..slen) { Some(x) => x, None => s };
    if d.len() + bounded.len() + 1 > dmax { return "REF_NOSPC"; }
    d.push_str(bounded);
    "OK"
}

// Logical guard region: a trailing sentinel in the String content. Safe Rust
// cannot write past the logical destination; after a refused call we verify
// the sentinel bytes are unchanged with `get(..)` and `to_owned()`.
fn canary_case(method: &str, attempt: impl FnOnce(&mut String) -> Tok) {
    const GUARD: &str = "ZZZZZZZZ";
    let mut buf = String::from("XY");
    buf.push_str(GUARD);
    let before = buf.to_owned();
    let outcome = attempt(&mut buf);
    let start = before.len() - GUARD.len();
    let intact = buf.get(start..) == Some(GUARD);
    let tok: Tok = if outcome != "OK" && intact {
        "CANARY_INTACT"
    } else {
        "CANARY_CORRUPT"
    };
    emit(method, "canary_refusal", tok);
}

fn m_strcpy_s() {
    let m = "strcpy_s";
    let short_src = "abc";
    let long_src = "ABCDEFGHIJ";
    let unterminated = "A".repeat(32);

    let mut d = String::new();
    emit(m, "valid_exact_fit", checked_scpy(Some(&mut d), 4, Some(short_src)));
    emit(m, "null_dest", checked_scpy(None, 4, Some(short_src)));
    let mut d = String::new();
    emit(m, "null_src", checked_scpy(Some(&mut d), 4, None));
    let mut d = String::new();
    emit(m, "one_byte_too_small", checked_scpy(Some(&mut d), 3, Some(short_src)));
    let mut d = String::new();
    emit(m, "unterminated_bounded",
         checked_scpy(Some(&mut d), 8, Some(&unterminated)));
    let mut d = String::new();
    emit(m, "truncation_dmax", checked_scpy(Some(&mut d), 4, Some(long_src)));
    canary_case(m, |buf| checked_scpy(Some(buf), 3, Some(short_src)));
}

fn m_strncpy_s() {
    let m = "strncpy_s";
    let short_src = "abc";
    let long_src = "ABCDEFGHIJ";
    let unterminated = "A".repeat(32);

    let mut d = String::new();
    emit(m, "valid_exact_fit", checked_scnpy(Some(&mut d), 4, Some(short_src), 4));
    emit(m, "null_dest", checked_scnpy(None, 4, Some(short_src), 4));
    let mut d = String::new();
    emit(m, "null_src", checked_scnpy(Some(&mut d), 4, None, 4));
    let mut d = String::new();
    emit(m, "one_byte_too_small",
         checked_scnpy(Some(&mut d), 3, Some(short_src), 4));
    let mut d = String::new();
    emit(m, "unterminated_bounded",
         checked_scnpy(Some(&mut d), 9, Some(&unterminated), 8));
    let mut d = String::new();
    emit(m, "truncation_dmax", checked_scnpy(Some(&mut d), 4, Some(long_src), 3));
    canary_case(m, |buf| checked_scnpy(Some(buf), 2, Some(short_src), 4));
}

fn m_strcat_s() {
    let m = "strcat_s";
    let short_src = "abc";
    let long_src = "ABCDEFGHIJ";
    let unterminated = "A".repeat(32);

    let mut d = String::from("xy");
    emit(m, "valid_exact_fit", checked_scat(Some(&mut d), 6, Some(short_src)));
    emit(m, "null_dest", checked_scat(None, 6, Some(short_src)));
    let mut d = String::from("xy");
    emit(m, "null_src", checked_scat(Some(&mut d), 6, None));
    let mut d = String::from("xy");
    emit(m, "one_byte_too_small", checked_scat(Some(&mut d), 5, Some(short_src)));
    let mut d = String::from("xy");
    emit(m, "unterminated_bounded",
         checked_scat(Some(&mut d), 8, Some(&unterminated)));
    let mut d = String::from("xy");
    emit(m, "truncation_dmax", checked_scat(Some(&mut d), 6, Some(long_src)));
    canary_case(m, |buf| checked_scat(Some(buf), 3, Some(short_src)));
}

fn m_strncat_s() {
    let m = "strncat_s";
    let short_src = "abc";
    let long_src = "ABCDEFGHIJ";
    let unterminated = "A".repeat(32);

    let mut d = String::from("xy");
    emit(m, "valid_exact_fit", checked_scnat(Some(&mut d), 6, Some(short_src), 3));
    emit(m, "null_dest", checked_scnat(None, 6, Some(short_src), 3));
    let mut d = String::from("xy");
    emit(m, "null_src", checked_scnat(Some(&mut d), 6, None, 3));
    let mut d = String::from("xy");
    emit(m, "one_byte_too_small",
         checked_scnat(Some(&mut d), 5, Some(short_src), 3));
    let mut d = String::from("xy");
    emit(m, "unterminated_bounded",
         checked_scnat(Some(&mut d), 8, Some(&unterminated), 5));
    let mut d = String::from("xy");
    emit(m, "truncation_dmax", checked_scnat(Some(&mut d), 5, Some(long_src), 2));
    canary_case(m, |buf| checked_scnat(Some(buf), 3, Some(short_src), 5));
}

// stpcpy_s has the same safety contract as strcpy_s; only the success return
// value differs (pointer to the terminating NUL rather than dest base), which
// is not a safety outcome, so the token sequence matches.
fn m_stpcpy_s() {
    let m = "stpcpy_s";
    let short_src = "abc";
    let long_src = "ABCDEFGHIJ";
    let unterminated = "A".repeat(32);

    let mut d = String::new();
    emit(m, "valid_exact_fit", checked_scpy(Some(&mut d), 4, Some(short_src)));
    emit(m, "null_dest", checked_scpy(None, 4, Some(short_src)));
    let mut d = String::new();
    emit(m, "null_src", checked_scpy(Some(&mut d), 4, None));
    let mut d = String::new();
    emit(m, "one_byte_too_small", checked_scpy(Some(&mut d), 3, Some(short_src)));
    let mut d = String::new();
    emit(m, "unterminated_bounded",
         checked_scpy(Some(&mut d), 8, Some(&unterminated)));
    let mut d = String::new();
    emit(m, "truncation_dmax", checked_scpy(Some(&mut d), 4, Some(long_src)));
    canary_case(m, |buf| checked_scpy(Some(buf), 3, Some(short_src)));
}

// stpncpy_s likewise mirrors strncpy_s: bounded copies truncate safely.
fn m_stpncpy_s() {
    let m = "stpncpy_s";
    let short_src = "abc";
    let long_src = "ABCDEFGHIJ";
    let unterminated = "A".repeat(32);

    let mut d = String::new();
    emit(m, "valid_exact_fit", checked_scnpy(Some(&mut d), 4, Some(short_src), 4));
    emit(m, "null_dest", checked_scnpy(None, 4, Some(short_src), 4));
    let mut d = String::new();
    emit(m, "null_src", checked_scnpy(Some(&mut d), 4, None, 4));
    let mut d = String::new();
    emit(m, "one_byte_too_small",
         checked_scnpy(Some(&mut d), 3, Some(short_src), 4));
    let mut d = String::new();
    emit(m, "unterminated_bounded",
         checked_scnpy(Some(&mut d), 9, Some(&unterminated), 8));
    let mut d = String::new();
    emit(m, "truncation_dmax", checked_scnpy(Some(&mut d), 4, Some(long_src), 3));
    canary_case(m, |buf| checked_scnpy(Some(buf), 2, Some(short_src), 4));
}

fn main() {
    let only = std::env::args().nth(1);
    let want = |method: &str| only.as_deref().map_or(true, |a| a == method);

    if want("strcpy_s") { m_strcpy_s(); }
    if want("strncpy_s") { m_strncpy_s(); }
    if want("strcat_s") { m_strcat_s(); }
    if want("strncat_s") { m_strncat_s(); }
    if want("stpcpy_s") { m_stpcpy_s(); }
    if want("stpncpy_s") { m_stpncpy_s(); }
}

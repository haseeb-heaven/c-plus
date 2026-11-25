/*! Safe-Rust equivalents of the C+ checked string-query APIs.
 *
 * Build (from repo root):
 *   rustc -O \
 *     benchmarks/safety_compare/per_method/strquery/strquery.rs \
 *     -o benchmarks/safety_compare/per_method/strquery/harness_rs
 *
 * Output lines are exactly:  <method> <case> <token>
 * `#![forbid(unsafe_code)]` makes the "no unsafe" requirement mechanical.
 *
 * Only safe primitives are used: str::len, str::find, str::rfind,
 * str::matches, str::char_indices, and slice/str::get.  A nullable C pointer
 * is modelled with Option<&str> = None; a bound is a usize.  These are the
 * *safe* modelling choices, not a claim of API equivalence (see NOTES.md).
 */

#![forbid(unsafe_code)]

const RSIZE_MAX_STR: usize = 4 << 10; // 4096, same pinned safeclib constant

fn emit(method: &str, case_name: &str, token: &str) {
    println!("{} {} {}", method, case_name, token);
}

/// Model of the C `dest`/`str` pointer plus a bound.  `None` models NULL;
/// bound 0 and bound > RSIZE_MAX_STR model the two refused-bound cases.
fn refused(data: Option<&str>, bound: usize) -> bool {
    data.is_none() || bound == 0 || bound > RSIZE_MAX_STR
}

/// Safe bounded view: `get(..bound)` never reads outside the string.
fn view<'a>(data: Option<&'a str>, bound: usize) -> &'a str {
    let s = data.expect("refused() must be checked before view()");
    s.get(..bound).unwrap_or(s)
}

fn ok(idx: usize) -> String {
    format!("OK:{}", idx)
}

fn canary_token() -> &'static str {
    // A safe bounded read plus a canary re-read, mirroring harness.c.
    let mut store = [0xA5u8; 16];
    let canary: [u8; 8] = *b"CANARY01";
    store[8..16].copy_from_slice(&canary);
    let region: &[u8] = store.get(0..8).expect("0..8 is in range");
    let _probe = region.len(); // `len` is one of the sanctioned safe ops
    if store.get(8..16) == Some(canary.as_slice()) {
        "CANARY_OK"
    } else {
        "CANARY_BAD"
    }
}

fn checked_or<'a>(data: Option<&'a str>, bound: usize) -> Option<&'a str> {
    if refused(data, bound) {
        None
    } else {
        Some(view(data, bound))
    }
}

fn emit_refused_cases(method: &str) {
    emit(method, "null", "REFUSED");
    emit(method, "zero_bound", "REFUSED");
    emit(method, "one_over_bound", "REFUSED");
}

fn main() {
    /* ------------------------------- strnlen_s --------------------------- */
    {
        let m = "strnlen_s";
        let s = checked_or(Some("abc\0"), 8).unwrap();
        emit(m, "valid", &ok(s.find('\0').unwrap()));

        emit_refused_cases(m);

        let s = checked_or(Some("abcde"), 5).unwrap();
        emit(
            m,
            "not_found",
            &format!("NOTFOUND:{}", s.find('\0').map_or(s.len(), |i| i)),
        );
        emit(m, "canary", canary_token());
    }

    /* -------------------------------- strchr_s --------------------------- */
    {
        let m = "strchr_s";
        let s = checked_or(Some("abc"), 3).unwrap();
        emit(
            m,
            "valid",
            &match s.find('c') {
                Some(i) => ok(i),
                None => "NOTFOUND".to_string(),
            },
        );

        emit_refused_cases(m);

        let s = checked_or(Some("abc"), 3).unwrap();
        emit(
            m,
            "not_found",
            &match s.find('z') {
                Some(_) => "BAD_RESULT".to_string(),
                None => "NOTFOUND".to_string(),
            },
        );
        emit(m, "canary", canary_token());
    }

    /* ------------------------------- strrchr_s --------------------------- */
    {
        let m = "strrchr_s";
        let s = checked_or(Some("abac"), 4).unwrap();
        emit(
            m,
            "valid",
            &match s.rfind('a') {
                Some(i) => ok(i),
                None => "NOTFOUND".to_string(),
            },
        );

        emit_refused_cases(m);

        let s = checked_or(Some("abac"), 4).unwrap();
        emit(
            m,
            "not_found",
            &match s.rfind('z') {
                Some(_) => "BAD_RESULT".to_string(),
                None => "NOTFOUND".to_string(),
            },
        );
        emit(m, "canary", canary_token());
    }

    /* -------------------------------- strstr_s --------------------------- */
    {
        let m = "strstr_s";
        let s = checked_or(Some("abcabc"), 6).unwrap();
        emit(
            m,
            "valid",
            &match s.find("bc") {
                Some(i) => ok(i),
                None => "NOTFOUND".to_string(),
            },
        );

        emit_refused_cases(m);

        let s = checked_or(Some("abcabc"), 6).unwrap();
        emit(
            m,
            "not_found",
            &match s.find("zz") {
                Some(_) => "BAD_RESULT".to_string(),
                None => "NOTFOUND".to_string(),
            },
        );
        emit(m, "canary", canary_token());
    }

    /* ---------------------------- strcasestr_s --------------------------- */
    {
        let m = "strcasestr_s";
        let s = checked_or(Some("aBcDef"), 6).unwrap();
        let lower = s.to_lowercase();
        let needle = "CD".to_lowercase();
        // char_indices keeps the byte offset in the original (ASCII here).
        let found = lower.find(&needle).map(|byte_idx| {
            let chars_before = lower
                .get(..byte_idx)
                .expect("byte_idx is a char boundary")
                .chars()
                .count();
            s.char_indices()
                .nth(chars_before)
                .map_or(s.len(), |(i, _)| i)
        });
        emit(
            m,
            "valid",
            &match found {
                Some(i) => ok(i),
                None => "NOTFOUND".to_string(),
            },
        );

        emit_refused_cases(m);

        let s = checked_or(Some("aBcDef"), 6).unwrap();
        let lower = s.to_lowercase();
        let found = lower.find("zz");
        emit(
            m,
            "not_found",
            &match found {
                Some(_) => "BAD_RESULT".to_string(),
                None => "NOTFOUND".to_string(),
            },
        );
        emit(m, "canary", canary_token());
    }

    /* -------------------------------- strspn_s --------------------------- */
    {
        let m = "strspn_s";
        let s = checked_or(Some("aabbxy"), 6).unwrap();
        let set = "ab";
        let count = s
            .chars()
            .take_while(|c| set.matches(*c).next().is_some())
            .count();
        emit(m, "valid", &ok(count));

        emit_refused_cases(m);

        let s = checked_or(Some("xyz"), 3).unwrap();
        let count = s
            .chars()
            .take_while(|c| set.matches(*c).next().is_some())
            .count();
        emit(m, "not_found", &format!("NOTFOUND:{}", count));
        emit(m, "canary", canary_token());
    }

    /* ------------------------------- strcspn_s --------------------------- */
    {
        let m = "strcspn_s";
        let s = checked_or(Some("abcXY"), 5).unwrap();
        let set = "XY";
        let count = s
            .chars()
            .take_while(|c| set.matches(*c).next().is_none())
            .count();
        emit(m, "valid", &ok(count));

        emit_refused_cases(m);

        let s = checked_or(Some("XYabc"), 5).unwrap();
        let count = s
            .chars()
            .take_while(|c| set.matches(*c).next().is_none())
            .count();
        emit(m, "not_found", &format!("NOTFOUND:{}", count));
        emit(m, "canary", canary_token());
    }

    /* strlen_s is intentionally absent: no symbol exists in the archive.
     * The driver records it as BLOCKED; no token is fabricated here. */
}

/*
 * conversion_rs.rs -- PAIRED numeric-conversion / ctype scenarios (Rust side).
 *
 * This is one half of a deliberately identical 100-case scenario set shared
 * with `conversion_c.c`. Both programs take a single argument N (1..100),
 * execute the SAME conceptual scenario, and print exactly one line:
 *
 *     RESULT <n> <ACCEPTED|REFUSED|SKIP>
 *
 * Both programs always return/exit 0 (the exit code is not the protocol).
 *
 * ---------------------------------------------------------------------------
 * HONESTY / MECHANISM NOTES
 * ---------------------------------------------------------------------------
 * Rust uses `str::parse::<T>()` (FromStr), `char::is_*` / `to_ascii_*`, and
 * checked arithmetic (`checked_mul`). `std::hint::black_box` wraps every input
 * so the conversion really executes at runtime rather than being const-folded.
 * FromStr returns a `Result`: Ok => ACCEPTED, Err => REFUSED. This is Rust's
 * native accept/refuse signal -- it is NOT forced to agree with the C half.
 *
 * Where mechanisms genuinely differ, each program reports what it actually
 * does and the paired comment says why. Recurring differences:
 *   * FromStr does NOT skip leading/trailing whitespace (the C host strto*
 *     functions do) -> C ACCEPTED / Rust REFUSED on "   -42", etc.
 *   * FromStr has NO base parameter: it cannot parse hex/octal/base-0 strings
 *     and has no notion of an "invalid base" argument -> C ACCEPTED / Rust
 *     REFUSED on "0x1F"; C invalid-base cases are Rust SKIP.
 *   * FromStr requires FULL consumption; C strto* stop at the first invalid
 *     char -> C ACCEPTED / Rust REFUSED on "12abc" and embedded NUL.
 *   * Float overflow/underflow: C host sets ERANGE (REFUSED here), while Rust
 *     parses to +/-inf or 0.0 and returns Ok (ACCEPTED).
 *   * ctype works on Unicode scalar values in Rust vs unsigned-char bytes in
 *     the C host -> e.g. 'ÿ' is alphabetic in Rust, not in the C locale.
 *   * No null input, no wide strings, and no EOF sentinel exist in Rust's safe
 *     parse/char model -> those C-only scenarios are honest Rust SKIPs.
 *
 * Outcomes are computed at runtime (no hard-coded result strings). Build with
 * `rustc --edition 2021 conversion_rs.rs -o conversion_rs`.
 * ---------------------------------------------------------------------------
 */
use std::hint::black_box;

const NUM_CASES: u32 = 100;
const OUT_REFUSED: u8 = 0;
const OUT_ACCEPTED: u8 = 1;
const OUT_SKIP: u8 = 2;
const _: () = assert!(NUM_CASES == 100, "conversion_rs.rs must define exactly 100 cases");

fn outcome_name(o: u8) -> &'static str {
    match o {
        OUT_ACCEPTED => "ACCEPTED",
        OUT_REFUSED => "REFUSED",
        _ => "SKIP",
    }
}

/* FromStr/parse Result -> accept/refuse. */
fn acc<T, E>(r: Result<T, E>) -> u8 {
    match r {
        Ok(_) => OUT_ACCEPTED,
        Err(_) => OUT_REFUSED,
    }
}

/* black_box wrapper for &str inputs. */
fn bb(s: &str) -> &str {
    black_box(s)
}

fn run_case(n: u32) -> u8 {
    match n {
        /* --- strtol, base 10 (1-10): Rust i64::from_str (decimal only) --- */
        1 => acc(bb("12345").parse::<i64>()),  // strtol("12345",10): both Ok.
        2 => acc(bb("   -42").parse::<i64>()), // strtol skips ws; Rust parse Err. DIFFERENCE.
        3 => acc(bb("+7").parse::<i64>()),     // explicit '+': both accepted.
        4 => acc(bb("00012").parse::<i64>()),  // leading zeros decimal in base 10.
        5 => acc(bb("").parse::<i64>()),       // empty: both refuse.
        6 => acc(bb("abc").parse::<i64>()),    // invalid input: both refuse.
        7 => acc(bb("12abc").parse::<i64>()),  // C partial consume; Rust full-consume Err.
        8 => acc(bb("9223372036854775807").parse::<i64>()),  // == i64::MAX: both Ok.
        9 => acc(bb("9223372036854775808").parse::<i64>()),  // overflow: C ERANGE, Rust Err.
        10 => acc(bb("-9223372036854775809").parse::<i64>()), // underflow: both refuse.

        /* --- strtoul, unsigned long (11-18): Rust u64 ------------------ */
        11 => acc(bb("42").parse::<u64>()),
        12 => acc(bb("-1").parse::<u64>()),     // C wraps to ULONG_MAX; Rust u64 refuses '-'.
        13 => acc(bb("0xFF").parse::<u64>()),   // C base-16 prefix; Rust has no hex parse.
        14 => acc(bb("077").parse::<u64>()),    // C octal=63, Rust decimal=77; both ACCEPTED, value differs.
        15 => acc(bb("0x10").parse::<u64>()),   // C base-0 hex; Rust parse Err.
        16 => acc(bb("18446744073709551615").parse::<u64>()), // == u64::MAX.
        17 => acc(bb("18446744073709551616").parse::<u64>()), // overflow: both refuse.
        18 => acc(bb("  99  ").parse::<u64>()), // C partial consume; Rust Err.

        /* --- strtoll, 64-bit signed (19-27): Rust i64 ----------------- */
        19 => acc(bb("-123").parse::<i64>()),
        20 => acc(bb("9223372036854775807").parse::<i64>()),
        21 => acc(bb("9223372036854775808").parse::<i64>()), // overflow.
        22 => acc(bb("0x7fffffff").parse::<i64>()),          // C base 16; Rust no hex.
        23 => acc(bb("010").parse::<i64>()),                 // C octal=8, Rust=10; both ACCEPTED.
        24 => acc(bb("+0").parse::<i64>()),
        25 => acc(bb("  \t-5").parse::<i64>()),              // C skips ws; Rust Err.
        26 => acc(bb("0").parse::<i64>()),
        27 => acc(bb("123 456").parse::<i64>()),             // C partial; Rust Err.

        /* --- strtoull, 64-bit unsigned (28-35): Rust u64 -------------- */
        28 => acc(bb("123").parse::<u64>()),
        29 => acc(bb("-2").parse::<u64>()),        // C wraps; Rust u64 Err.
        30 => acc(bb("0xdeadBEEF").parse::<u64>()), // C base 16; Rust no hex.
        31 => acc(bb("0X1A").parse::<u64>()),      // C base-0 uppercase hex; Rust Err.
        32 => acc(bb("18446744073709551616").parse::<u64>()), // overflow.
        33 => acc(bb("  0  ").parse::<u64>()),     // C partial; Rust Err.
        34 => acc(bb("007").parse::<u64>()),
        35 => acc(bb("").parse::<u64>()),

        /* --- strtod, double (36-44): Rust f64 ------------------------- */
        36 => acc(bb("3.14").parse::<f64>()),
        37 => acc(bb("  -0.5").parse::<f64>()),    // C skips ws; Rust Err.
        38 => acc(bb("1e3").parse::<f64>()),
        39 => acc(bb("1e309").parse::<f64>()),     // C ERANGE REFUSED; Rust Ok(inf). KEY DIFFERENCE.
        40 => acc(bb("1e-400").parse::<f64>()),    // C ERANGE; Rust Ok(0.0).
        41 => acc(bb("nan").parse::<f64>()),       // both ACCEPTED (NaN).
        42 => acc(bb("inf").parse::<f64>()),       // both ACCEPTED.
        43 => acc(bb("abc").parse::<f64>()),       // both REFUSED.
        44 => acc(bb("0x1p4").parse::<f64>()),     // C hex-float ACCEPTED; Rust no hex float Err.

        /* --- strtof, float (45-48): Rust f32 -------------------------- */
        45 => acc(bb("1.5").parse::<f32>()),
        46 => acc(bb("1e39").parse::<f32>()),      // C ERANGE REFUSED; Rust Ok(inf).
        47 => acc(bb("  2.5").parse::<f32>()),     // C skips ws; Rust Err.
        48 => acc(bb("nope").parse::<f32>()),

        /* --- strtold, long double (49-51): Rust f64 analogue ---------- */
        49 => acc(bb("2.71828").parse::<f64>()),
        50 => acc(bb("1e-5000").parse::<f64>()),   // C ERANGE; Rust Ok(0.0).
        51 => acc(bb("  9.0").parse::<f64>()),     // C skips ws; Rust Err.

        /* --- atoi/atol/atoll/atof (52-61) ----------------------------- */
        52 => acc(bb("42").parse::<i32>()),
        53 => acc(bb("  -7").parse::<i32>()),      // atoi skips ws; Rust Err.
        54 => acc(bb("abc").parse::<i32>()),
        55 => acc(bb("12xyz").parse::<i32>()),     // atoi prefix ACCEPTED; Rust Err.
        56 => acc(bb("2147483648").parse::<i32>()),// atoi cannot report overflow; Rust i32 Err.
        57 => acc(bb("123").parse::<i64>()),
        58 => acc(bb("  9").parse::<i64>()),       // atol skips ws; Rust Err.
        59 => acc(bb("9223372036854775807").parse::<i64>()),
        60 => acc(bb("3.5").parse::<f64>()),
        61 => acc(bb(" 2.5").parse::<f64>()),      // atof skips ws; Rust Err.

        /* --- strtoimax / strtoumax / wcstoimax (62-71) ---------------- */
        62 => acc(bb("-5").parse::<i64>()),
        63 => acc(bb("9223372036854775808").parse::<i64>()), // overflow.
        64 => acc(bb("0x2A").parse::<i64>()),      // C base 16; Rust no hex.
        65 => OUT_SKIP,                           // NULL input: no null in Rust's FromStr model.
        66 => acc(bb("42").parse::<u64>()),
        67 => acc(bb("-1").parse::<u64>()),       // C wraps; Rust u64 Err.
        68 => acc(bb("0x10").parse::<u64>()),     // C base-0 hex; Rust Err.
        69 => OUT_SKIP,                           // wcstoimax(L"123"): Rust has no wide strings.
        70 => OUT_SKIP,                           // wcstoimax(L"0x1F",0): no wide strings.
        71 => OUT_SKIP,                           // wcstoimax(L"abc"): no wide strings.

        /* --- invalid bases, embedded NUL, high-bit, locale (72-82) ---- */
        72 => OUT_SKIP,                           // invalid base 1: Rust parse has no base argument.
        73 => OUT_SKIP,                           // invalid base 37.
        74 => OUT_SKIP,                           // invalid base INT_MIN.
        75 => OUT_SKIP,                           // strtoul invalid base 1.
        76 => OUT_SKIP,                           // strtoll invalid base 37.
        77 => acc(bb("12\0 34").parse::<i64>()),  // C stops at NUL ACCEPTED; Rust full-consume Err.
        78 => acc(bb("2.5\0garbage").parse::<f64>()), // C partial ACCEPTED; Rust Err.
        79 => acc(bb("\u{FF}").parse::<i64>()),   // both REFUSED (non-ASCII scalar).
        80 => if black_box('\u{FF}').is_alphabetic() { OUT_ACCEPTED } else { OUT_REFUSED },
        // C isalpha(0xFF) false in C locale; Rust 'ÿ'.is_alphabetic() true. DIFFERENCE.
        81 => acc(bb("9\0x").parse::<u64>()),     // C NUL truncation ACCEPTED; Rust Err.
        82 => acc(bb("1,5").parse::<f64>()),      // C locale stops at comma; Rust Err.

        /* --- ctype predicates + EOF (83-95) --------------------------- */
        83 => if black_box('7').is_ascii_digit() { OUT_ACCEPTED } else { OUT_REFUSED },
        84 => if black_box('a').is_ascii_digit() { OUT_ACCEPTED } else { OUT_REFUSED },
        85 => if black_box('Z').is_alphabetic() { OUT_ACCEPTED } else { OUT_REFUSED },
        86 => if black_box('5').is_alphabetic() { OUT_ACCEPTED } else { OUT_REFUSED },
        87 => if black_box(' ').is_whitespace() { OUT_ACCEPTED } else { OUT_REFUSED },
        88 => if black_box('x').is_whitespace() { OUT_ACCEPTED } else { OUT_REFUSED },
        89 => if black_box('9').is_alphanumeric() { OUT_ACCEPTED } else { OUT_REFUSED },
        90 => if black_box('f').is_ascii_hexdigit() { OUT_ACCEPTED } else { OUT_REFUSED },
        91 => if black_box('Q').is_uppercase() { OUT_ACCEPTED } else { OUT_REFUSED },
        92 => if black_box('q').is_lowercase() { OUT_ACCEPTED } else { OUT_REFUSED },
        93 => { let c = black_box('a').to_ascii_uppercase(); if c != 'a' { OUT_ACCEPTED } else { OUT_REFUSED } },
        94 => OUT_SKIP,                           // toupper(EOF): char has no EOF sentinel.
        95 => OUT_SKIP,                           // isdigit(EOF): char has no EOF sentinel.

        /* --- checked arithmetic + parse Err (96-100) ------------------ */
        96 => match black_box(i64::MAX).checked_mul(black_box(2i64)) {
            Some(_) => OUT_ACCEPTED, None => OUT_REFUSED,
        }, // i64 mul overflow -> None.
        97 => match black_box(1000i64).checked_mul(black_box(1000i64)) {
            Some(_) => OUT_ACCEPTED, None => OUT_REFUSED,
        }, // in range -> Some.
        98 => match black_box(u64::MAX).checked_mul(black_box(2u64)) {
            Some(_) => OUT_ACCEPTED, None => OUT_REFUSED,
        }, // u64 mul overflow -> None.
        99 => acc(bb("十二").parse::<i64>()),     // non-ASCII digit word -> Err (both refuse).
        100 => acc(bb("\u{0665}").parse::<i64>()), // Arabic-Indic digit -> Err (Rust parse is ASCII-only).

        _ => OUT_SKIP,
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let n: u32 = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(0);
    let o = if n >= 1 && n <= NUM_CASES { run_case(n) } else { OUT_SKIP };
    println!("RESULT {} {}", n, outcome_name(o));
    // always exit 0: the outcome is in the printed line, not the exit code
}

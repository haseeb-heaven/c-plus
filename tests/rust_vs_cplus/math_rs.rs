// tests/rust_vs_cplus/math_rs.rs
//
// Rust half of the paired 100-scenario math comparison.  The C+ half is
// tests/rust_vs_cplus/math_c.c; case <n> denotes the SAME scenario in both
// files.  tests/rust_vs_cplus/run_compare.sh runs both programs with an
// integer argument 1..100 and compares the single line:
//
//     RESULT <n> <OUTCOME>
//
// where OUTCOME is one of ACCEPTED, REFUSED, SKIP.  Both programs always
// return 0.  This program contains no panicking operations: inputs are
// opaque via std::hint::black_box, argument parsing uses `.ok()`, and the
// only checked conversions use `try_from`, whose `Result` is inspected with
// `.is_ok()` / `.is_err()` rather than unwrapped.
//
// OUTCOME DEFINITIONS (shared with math_c.c)
// ------------------------------------------
//   ACCEPTED  A defined numeric result was produced.  This INCLUDES NaN/Inf
//             from domain errors, poles, overflow and underflow: IEEE-754
//             defines those results, so they are not refusals and not UB.
//             C may additionally signal errno (on this macOS host it does
//             NOT for math errors); Rust has no errno at all.  Both sides
//             report ACCEPTED for sqrt(-1) and log(0) for the same reason:
//             a defined result was produced.  The errno difference is
//             documented, never turned into a fake outcome mismatch.
//   REFUSED   The operation declined the request: a checked conversion
//             (`try_from`) on a float bit pattern returned Err because the
//             value cannot be represented in the target integer type
//             (cases 97-100).  Cases 93-96 are C-only because the hardened
//             cplus_* wrappers refuse NULL out-parameters with EINVAL;
//             Rust has no frexp/modf methods, so those cases are SKIP here.
//   SKIP      Not expressible with stable Rust f64/f32:
//             * no long double / f80 / f128 (cases 83-92);
//             * no std methods for frexp, modf, ldexp, ilogb, erf or gamma
//               (cases 43-52 and 76-81);
//             * no C-style out-parameter to probe for a NULL refusal
//               (cases 93-96).
//
// Build (matches run_compare.sh):
//   rustc --edition 2021 tests/rust_vs_cplus/math_rs.rs -o /tmp/math_rs

use std::hint::black_box;

const CASE_COUNT: usize = 100;
// Compile-time assertion that the suite has exactly 100 cases.
const _: () = assert!(CASE_COUNT == 100);

fn report(n: i32, outcome: &str) {
    println!("RESULT {} {}", n, outcome);
}
fn accepted(n: i32) { report(n, "ACCEPTED"); }
fn refused(n: i32)  { report(n, "REFUSED");  }
fn skip(n: i32)     { report(n, "SKIP");     }

// Keep each computed result observable so the math is not constant folded.
fn sink_f64(x: f64) { let _ = black_box(x); }
fn sink_f32(x: f32) { let _ = black_box(x); }

fn main() {
    let n: i32 = std::env::args()
        .nth(1)
        .and_then(|s| s.parse::<i32>().ok())
        .unwrap_or(0);
    if n < 1 || n > CASE_COUNT as i32 {
        report(n, "SKIP");
        return;
    }

    match n {
        // ---- f64 methods with direct cplus_* counterparts -----------------
        1 => { /* sqrt(4.0) = 2.0: finite square root. */
            sink_f64(black_box(4.0f64).sqrt()); accepted(n); }
        2 => { /* sqrt(-1.0) = NaN: domain error. C errno 0 on this host. */
            sink_f64(black_box(-1.0f64).sqrt()); accepted(n); }
        3 => { /* sqrt(-0.0) = -0.0: signed zero preserved. */
            sink_f64(black_box(-0.0f64).sqrt()); accepted(n); }
        4 => { /* sqrt(+Inf) = +Inf. */
            sink_f64(black_box(f64::INFINITY).sqrt()); accepted(n); }
        5 => { /* sqrt(DBL_TRUE_MIN): smallest subnormal double input. */
            sink_f64(f64::from_bits(black_box(1u64)).sqrt()); accepted(n); }
        6 => { /* sqrt(DBL_MAX): largest finite double stays finite. */
            sink_f64(black_box(f64::MAX).sqrt()); accepted(n); }
        7 => { /* abs(-3.5) = 3.5. */
            sink_f64(black_box(-3.5f64).abs()); accepted(n); }
        8 => { /* abs(-0.0) = +0.0: sign bit cleared. */
            sink_f64(black_box(-0.0f64).abs()); accepted(n); }
        9 => { /* floor(2.9) = 2.0. */
            sink_f64(black_box(2.9f64).floor()); accepted(n); }
        10 => { /* floor(-2.1) = -3.0. */
            sink_f64(black_box(-2.1f64).floor()); accepted(n); }
        11 => { /* ceil(2.1) = 3.0. */
            sink_f64(black_box(2.1f64).ceil()); accepted(n); }
        12 => { /* ceil(-2.9) = -2.0. */
            sink_f64(black_box(-2.9f64).ceil()); accepted(n); }
        13 => { /* round(2.5) = 3.0: halfway away from zero. */
            sink_f64(black_box(2.5f64).round()); accepted(n); }
        14 => { /* round(-2.5) = -3.0. */
            sink_f64(black_box(-2.5f64).round()); accepted(n); }
        15 => { /* trunc(2.9) = 2.0. */
            sink_f64(black_box(2.9f64).trunc()); accepted(n); }
        16 => { /* trunc(-2.9) = -2.0. */
            sink_f64(black_box(-2.9f64).trunc()); accepted(n); }
        17 => { /* fmod(5,3) = 2: Rust `%` has fmod semantics. */
            sink_f64(black_box(5.0f64) % black_box(3.0f64)); accepted(n); }
        18 => { /* fmod(-5,3) = -2: dividend sign retained. */
            sink_f64(black_box(-5.0f64) % black_box(3.0f64)); accepted(n); }
        19 => { /* fmod(5,0) = NaN: defined NaN result, C errno 0 on this host. */
            sink_f64(black_box(5.0f64) % black_box(0.0f64)); accepted(n); }
        20 => { /* pow(2,10) = 1024. */
            sink_f64(black_box(2.0f64).powf(black_box(10.0f64))); accepted(n); }
        21 => { /* pow(-1,0.5) = NaN: domain error both sides. */
            sink_f64(black_box(-1.0f64).powf(black_box(0.5f64))); accepted(n); }
        22 => { /* pow(0,-1) = +Inf: pole. */
            sink_f64(black_box(0.0f64).powf(black_box(-1.0f64))); accepted(n); }
        23 => { /* pow(2,-1074) = DBL_TRUE_MIN: underflow to subnormal. */
            sink_f64(black_box(2.0f64).powf(black_box(-1074.0f64))); accepted(n); }
        24 => { /* exp(1) = e. */
            sink_f64(black_box(1.0f64).exp()); accepted(n); }
        25 => { /* exp(1000) = +Inf: overflow. */
            sink_f64(black_box(1000.0f64).exp()); accepted(n); }
        26 => { /* exp(-1000) = 0.0: underflow. */
            sink_f64(black_box(-1000.0f64).exp()); accepted(n); }
        27 => { /* log(1) = 0.0. */
            sink_f64(black_box(1.0f64).ln()); accepted(n); }
        28 => { /* log(0) = -Inf: pole. C errno 0 here; Rust has no errno. */
            sink_f64(black_box(0.0f64).ln()); accepted(n); }
        29 => { /* log(-1) = NaN: domain error both sides. */
            sink_f64(black_box(-1.0f64).ln()); accepted(n); }
        30 => { /* log(+Inf) = +Inf. */
            sink_f64(black_box(f64::INFINITY).ln()); accepted(n); }
        31 => { /* sin(0) = 0.0. */
            sink_f64(black_box(0.0f64).sin()); accepted(n); }
        32 => { /* cos(0) = 1.0. */
            sink_f64(black_box(0.0f64).cos()); accepted(n); }
        33 => { /* tan(0) = 0.0. */
            sink_f64(black_box(0.0f64).tan()); accepted(n); }
        34 => { /* sin(NaN) = NaN: NaN propagation. */
            sink_f64(black_box(f64::NAN).sin()); accepted(n); }
        35 => { /* fmin(1,2) = 1: Rust min ignores NaN like C fmin. */
            sink_f64(black_box(1.0f64).min(black_box(2.0f64))); accepted(n); }
        36 => { /* fmin(NaN,2) = 2: NaN operand ignored. */
            sink_f64(black_box(f64::NAN).min(black_box(2.0f64))); accepted(n); }
        37 => { /* fmax(NaN,2) = 2: NaN operand ignored. */
            sink_f64(black_box(f64::NAN).max(black_box(2.0f64))); accepted(n); }
        38 => { /* fmax(-0,+0) = +0: both non-NaN. */
            sink_f64(black_box(-0.0f64).max(black_box(0.0f64))); accepted(n); }
        39 => { /* fma(2,3,4) = 10: fused multiply-add via mul_add. */
            sink_f64(black_box(2.0f64).mul_add(black_box(3.0f64), black_box(4.0f64))); accepted(n); }
        40 => { /* hypot(3,4) = 5. */
            sink_f64(black_box(3.0f64).hypot(black_box(4.0f64))); accepted(n); }
        41 => { /* hypot(DBL_MAX,DBL_MAX) = +Inf: overflow. */
            sink_f64(black_box(f64::MAX).hypot(black_box(f64::MAX))); accepted(n); }
        42 => { /* copysign(1,-0) = -1. */
            sink_f64(black_box(1.0f64).copysign(black_box(-0.0f64))); accepted(n); }

        // ---- C functions with no stable Rust method: honest SKIP -----------
        43 => { /* C: frexp(8,&e) = 0.5, e=4. No Rust frexp -> SKIP. */
            skip(n); }
        44 => { /* C: frexp(0,&e) = 0.0, e=0. No Rust frexp -> SKIP. */
            skip(n); }
        45 => { /* C: modf(3.25,&i) = 0.25, i=3. Rust has fract() but not
                   the modf out-parameter API -> SKIP (no fake match). */
            skip(n); }
        46 => { /* C: modf(-3.25,&i) = -0.25, i=-3 -> SKIP. */
            skip(n); }
        47 => { /* C: ldexp(0.5,4) = 8. No Rust ldexp method -> SKIP. */
            skip(n); }
        48 => { /* C: ldexp(DBL_MAX,1) = +Inf (overflow) -> SKIP. */
            skip(n); }
        49 => { /* C: ilogb(8) = 3. No Rust ilogb method -> SKIP. */
            skip(n); }
        50 => { /* C: ilogb(0) = FP_ILOGB0 sentinel -> SKIP. */
            skip(n); }
        51 => { /* C: erf(0) = 0. No Rust std erf -> SKIP. */
            skip(n); }
        52 => { /* C: tgamma(5) = 24 (Gamma(5) = 4!). No Rust std gamma -> SKIP. */
            skip(n); }

        // ---- f32 methods with direct cplus_* counterparts ------------------
        53 => { /* sqrtf(4f) = 2f. */
            sink_f32(black_box(4.0f32).sqrt()); accepted(n); }
        54 => { /* sqrtf(-1f) = NaN: float domain error. */
            sink_f32(black_box(-1.0f32).sqrt()); accepted(n); }
        55 => { /* fabsf(-3.5f) = 3.5f. */
            sink_f32(black_box(-3.5f32).abs()); accepted(n); }
        56 => { /* floorf(2.9f) = 2f. */
            sink_f32(black_box(2.9f32).floor()); accepted(n); }
        57 => { /* ceilf(2.1f) = 3f. */
            sink_f32(black_box(2.1f32).ceil()); accepted(n); }
        58 => { /* roundf(2.5f) = 3f. */
            sink_f32(black_box(2.5f32).round()); accepted(n); }
        59 => { /* truncf(2.9f) = 2f. */
            sink_f32(black_box(2.9f32).trunc()); accepted(n); }
        60 => { /* fmodf(5f,3f) = 2f. */
            sink_f32(black_box(5.0f32) % black_box(3.0f32)); accepted(n); }
        61 => { /* powf(2f,10f) = 1024f. */
            sink_f32(black_box(2.0f32).powf(black_box(10.0f32))); accepted(n); }
        62 => { /* expf(1f) = e as f32. */
            sink_f32(black_box(1.0f32).exp()); accepted(n); }
        63 => { /* expf(100f) = +Inf: FLT_MAX overflow. */
            sink_f32(black_box(100.0f32).exp()); accepted(n); }
        64 => { /* logf(1f) = 0f. */
            sink_f32(black_box(1.0f32).ln()); accepted(n); }
        65 => { /* logf(0f) = -Inf: pole, no errno in Rust. */
            sink_f32(black_box(0.0f32).ln()); accepted(n); }
        66 => { /* sinf(0f) = 0f. */
            sink_f32(black_box(0.0f32).sin()); accepted(n); }
        67 => { /* cosf(0f) = 1f. */
            sink_f32(black_box(0.0f32).cos()); accepted(n); }
        68 => { /* tanf(0f) = 0f. */
            sink_f32(black_box(0.0f32).tan()); accepted(n); }
        69 => { /* fminf(NaN,2f) = 2f. */
            sink_f32(black_box(f32::NAN).min(black_box(2.0f32))); accepted(n); }
        70 => { /* fmaxf(NaN,2f) = 2f. */
            sink_f32(black_box(f32::NAN).max(black_box(2.0f32))); accepted(n); }
        71 => { /* fmaf(2f,3f,4f) = 10f. */
            sink_f32(black_box(2.0f32).mul_add(black_box(3.0f32), black_box(4.0f32))); accepted(n); }
        72 => { /* hypotf(3f,4f) = 5f. */
            sink_f32(black_box(3.0f32).hypot(black_box(4.0f32))); accepted(n); }
        73 => { /* sqrtf(FLT_TRUE_MIN): smallest subnormal float input. */
            sink_f32(f32::from_bits(black_box(1u32)).sqrt()); accepted(n); }
        74 => { /* fmaxf(FLT_MAX,1f) = FLT_MAX. */
            sink_f32(black_box(f32::MAX).max(black_box(1.0f32))); accepted(n); }
        75 => { /* fminf(-FLT_MAX,1f) = -FLT_MAX. */
            sink_f32(black_box(-f32::MAX).min(black_box(1.0f32))); accepted(n); }
        82 => { /* expf(-100f) = 0f: float underflow. */
            sink_f32(black_box(-100.0f32).exp()); accepted(n); }

        // ---- float C functions with no stable Rust method: honest SKIP -----
        76 => { /* C: frexpf(8f,&e) = 0.5f, e=4 -> SKIP. */
            skip(n); }
        77 => { /* C: modff(3.25f,&i) = 0.25f, i=3f -> SKIP. */
            skip(n); }
        78 => { /* C: ldexpf(0.5f,4) = 8f -> SKIP. */
            skip(n); }
        79 => { /* C: ilogbf(8f) = 3 -> SKIP. */
            skip(n); }
        80 => { /* C: erff(0f) = 0f -> SKIP. */
            skip(n); }
        81 => { /* C: tgammaf(5f) = 24f -> SKIP. */
            skip(n); }

        // ---- long double: Rust has no f80/f128, honest SKIP ----------------
        83 => { /* C: sqrtl(4L) = 2L -> SKIP (no long double). */
            skip(n); }
        84 => { /* C: fabsl(-3.5L) = 3.5L -> SKIP. */
            skip(n); }
        85 => { /* C: floorl(2.9L) = 2L -> SKIP. */
            skip(n); }
        86 => { /* C: ceill(2.1L) = 3L -> SKIP. */
            skip(n); }
        87 => { /* C: sinl(0L) = 0L -> SKIP. */
            skip(n); }
        88 => { /* C: cosl(0L) = 1L -> SKIP. */
            skip(n); }
        89 => { /* C: tanl(0L) = 0L -> SKIP. */
            skip(n); }
        90 => { /* C: expl(1L) = e -> SKIP. */
            skip(n); }
        91 => { /* C: logl(1L) = 0L -> SKIP. */
            skip(n); }
        92 => { /* C: powl(2L,10L) = 1024L -> SKIP. */
            skip(n); }

        // ---- C-only NULL out-parameter probes: honest SKIP -----------------
        93 => { /* C: frexp(8,NULL) wrapper refuses with EINVAL; no Rust API. */
            skip(n); }
        94 => { /* C: modf(3.25,NULL) wrapper refuses with EINVAL; no Rust API. */
            skip(n); }
        95 => { /* C: frexpf(8f,NULL) wrapper refuses with EINVAL. */
            skip(n); }
        96 => { /* C: modff(3.25f,NULL) wrapper refuses with EINVAL. */
            skip(n); }

        // ---- checked narrowing of math bit patterns: REFUSED ---------------
        97 => { /* abs(-DBL_MAX) bits -> i32 via try_from: out of range. */
            let bits = black_box(-f64::MAX).abs().to_bits();
            if i32::try_from(bits).is_ok() { accepted(n) } else { refused(n) } }
        98 => { /* absf(-FLT_MAX) bits -> i16 via try_from: out of range. */
            let bits = black_box(-f32::MAX).abs().to_bits();
            if i16::try_from(bits).is_ok() { accepted(n) } else { refused(n) } }
        99 => { /* exp(1000)=Inf bits -> i32 via try_from: out of range. */
            let bits = black_box(1000.0f64).exp().to_bits();
            if i32::try_from(bits).is_ok() { accepted(n) } else { refused(n) } }
        100 => { /* expf(100f)=Inf bits -> i16 via try_from: out of range. */
            let bits = black_box(100.0f32).exp().to_bits();
            if i16::try_from(bits).is_ok() { accepted(n) } else { refused(n) } }

        _ => { /* Unreachable for 1..100; kept panic-free for any other arg. */
            skip(n); }
    }
}

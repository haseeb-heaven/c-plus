// Rust side of the native safety-outcome matrix (SAFE Rust ONLY: no `unsafe`).
//
// Build/run (from the repo root):
//   rustc --edition 2021 benchmarks/safety_compare/safety_matrix.rs -o <out>
//   timeout 30 <out>
//
// Compile-only borrow-check rejection (never runs, by construction):
//   rustc --edition 2021 --cfg UAF_COMPILE_REJECT \
//     benchmarks/safety_compare/safety_matrix.rs -o <out>   # exits non-zero
//
// IMPORTANT: outcome-token equality with safety_matrix.cp is evidence of
// *observed behaviour* only, NOT of equivalent guarantees. C+ enforces these
// contracts at run time through checked runtime APIs; Rust rejects several of
// the same situations at compile time (no null references, no use-after-move,
// no second ownership release) and uses checked return values for the rest.
// The Rust "failure injection" model is an explicit capacity budget, not a
// simulated allocator OOM.

#![allow(unexpected_cfgs)]

use std::hint::black_box;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Outcome {
    Ok,
    Refused,
}

impl Outcome {
    fn as_str(self) -> &'static str {
        match self {
            Outcome::Ok => "OK",
            Outcome::Refused => "REFUSED",
        }
    }
}

fn emit(id: &str, outcome: Outcome, detail: &str) {
    println!("SCENARIO {} {} {}", id, outcome.as_str(), detail);
}

#[derive(Debug, PartialEq, Eq)]
enum CopyError {
    NullArgument,
    TooSmall,
}

/// Checked-copy contract: a null destination is unrepresentable as `&mut [u8]`
/// in safe Rust, so the "null" case is carried explicitly as Option::None.
fn copy_checked(dest: Option<&mut [u8]>, src: &[u8]) -> Result<usize, CopyError> {
    let d = dest.ok_or(CopyError::NullArgument)?;
    if src.len() > d.len() {
        return Err(CopyError::TooSmall);
    }
    d[..src.len()].copy_from_slice(src);
    Ok(src.len())
}

// 1. null argument refusal -------------------------------------------------
fn s01_null_argument() {
    let src = [1u8, 2, 3, 4, 5, 6, 7, 8];
    let dest: Option<&mut [u8]> = None; // safe Rust has no null &T
    let r = copy_checked(dest, &src);
    emit(
        "01_null_argument",
        if r == Err(CopyError::NullArgument) {
            Outcome::Refused
        } else {
            Outcome::Ok
        },
        "copy_checked(dest=None) -> Err(NullArgument); null &T is unrepresentable",
    );
}

// 2. zero-length buffer ----------------------------------------------------
fn s02_zero_length() {
    let v: Vec<u8> = Vec::new();
    emit(
        "02_zero_length",
        Outcome::Ok,
        "Vec::new() len=0 (defined; C+ malloc_s(0) reports tracked_size=1)",
    );
    black_box(v.len());
}

// 3. exact-fit / one-over --------------------------------------------------
fn s03_exact_fit() {
    let src = [1u8, 2, 3, 4, 5, 6, 7, 8];
    let mut buf = [0u8; 8];
    let r = copy_checked(Some(&mut buf), &src);
    emit(
        "03a_exact_fit",
        if r.is_ok() { Outcome::Ok } else { Outcome::Refused },
        "copy_checked(dmax=8,smax=8) -> Ok(8)",
    );
    black_box(buf);
}

fn s03_one_over() {
    let src = [1u8, 2, 3, 4, 5, 6, 7, 8, 9];
    let mut buf = [0u8; 8];
    let r = copy_checked(Some(&mut buf), &src);
    emit(
        "03b_one_over",
        if r == Err(CopyError::TooSmall) {
            Outcome::Refused
        } else {
            Outcome::Ok
        },
        "copy_checked(dmax=8,smax=9) -> Err(TooSmall); raw buf[8] would panic",
    );
    black_box(buf);
}

// 4. integer overflow refusal ---------------------------------------------
fn s04_integer_overflow() {
    let r = usize::MAX.checked_mul(2);
    emit(
        "04_integer_overflow",
        if r.is_none() { Outcome::Refused } else { Outcome::Ok },
        "usize::MAX.checked_mul(2) -> None",
    );
    black_box(r);
}

// 5. allocation failure atomicity -----------------------------------------
fn s05_alloc_atomicity() {
    let mut v = vec![0x5au8; 32];
    let budget = 0usize; // explicit capacity model, not a simulated OOM
    let extra = 64usize.saturating_sub(v.len());
    let r: Result<(), ()> = if extra > budget {
        Err(())
    } else {
        match v.try_reserve_exact(extra) {
            Ok(()) => {
                v.resize(64, 0);
                Ok(())
            }
            Err(_) => Err(()),
        }
    };
    let intact = v.len() == 32 && v[0] == 0x5a && v[31] == 0x5a;
    emit(
        "05_alloc_atomicity",
        if r.is_err() && intact {
            Outcome::Refused
        } else {
            Outcome::Ok
        },
        "budget=0 reserve -> Err; original len=32 bytes_intact (atomic)",
    );
    black_box(&v);
}

// 6. use-after-drop: runtime model ----------------------------------------
fn s06_use_after_drop_runtime() {
    let mut slot = Some(Box::new(7u8));
    let first = slot.take();
    black_box(first); // the single legal release
    let second = slot.take(); // None: a second release cannot be produced
    emit(
        "06_use_after_drop_runtime",
        if second.is_none() {
            Outcome::Refused
        } else {
            Outcome::Ok
        },
        "Option::take() twice -> second=None (no second owner in safe Rust)",
    );
    black_box(second);
}

// 6. use-after-drop: COMPILE-ONLY borrow-check rejection ------------------
// Not part of the runtime matrix. Compiling with `--cfg UAF_COMPILE_REJECT`
// fails with E0382 ("borrow of moved value"), so no binary is produced.
#[cfg(UAF_COMPILE_REJECT)]
fn uaf_compile_reject() {
    let value = Box::new(7u8);
    drop(value); // moves `value` into drop
    println!("{}", *value); // E0382: borrow of moved value `value`
    black_box(value);
}

// 7. double-drop refusal ---------------------------------------------------
fn s07_double_drop() {
    let mut slot = Some(vec![0u8; 16]);
    let first = slot.take(); // Some
    let second = slot.take(); // None
    emit(
        "07_double_drop",
        if first.is_some() && second.is_none() {
            Outcome::Refused
        } else {
            Outcome::Ok
        },
        "Option::take() first=Some second=None (second drop refused)",
    );
    black_box((first, second));
}

fn main() {
    #[cfg(UAF_COMPILE_REJECT)]
    uaf_compile_reject();

    s01_null_argument();
    s02_zero_length();
    s03_exact_fit();
    s03_one_over();
    s04_integer_overflow();
    s05_alloc_atomicity();
    s06_use_after_drop_runtime();
    s07_double_drop();
    println!("MATRIX_DONE 8");
}

# C+ compile-time arithmetic enforcement (`CPLUS-ARITH`)

**Status: evidence-only.** This document records what the current C+ checker
actually enforces at compile time for integer arithmetic, the exact diagnostics,
the reproductions that drove the work, the regression commands, and the remaining
gaps. Passing suites prove only the cases they exercise. **No blanket claim is
made that C+ programs are free of arithmetic undefined behaviour, nor that C+ is
equivalent to Rust.**

Implementation references use **function/symbol names** in
`compiler/Sema/AstSafetyCheck.cpp`, not line numbers: the working tree is being
updated in parallel, so line numbers drift. The behaviour described here
corresponds to commits `7e44f81` ("compile-time signed overflow and
division-by-zero analysis") and `cbb6e3d` ("catch demo.cp overflow through
volatile locals and helpers"). Observed diagnostics below were captured from
`build/c+` with `CPLUS_CLANG=/usr/bin/clang`; in-flight additions not present in
that binary (e.g. shift-count checks) are outside this document's scope.

---

## 1. What is enforced, and when

The checks are **C+ AST analysis**, not UBSan instrumentation. They run in both
`c+ check` and `c+ build`, including `c+ build --strict --sanitize=none`.

| Operation | Condition for a finding | `check --strict` | `check --default` |
| --- | --- | --- | --- |
| Signed `+`, `-`, `*` | Both operands are compile-time-known and the mathematical result is outside the source-width signed range | error | warning |
| `/` or `%` | Right operand (divisor) is a compile-time-known zero | error | warning |
| `/` or `%` | Divisor is not known constant **and** the checker cannot prove it non-zero | error | warning |
| Unsigned `+`, `-`, `*` (defined wrapping) | — | accepted | accepted |
| `/` or `%` with a divisor the checker proves non-zero | — | accepted | accepted |

Important limits that are part of the design, not incidental:

- Overflow is computed with a wider `__int128` intermediate and compared against
  `[-2^(w-1), 2^(w-1)-1]`, where `w = ctx_.getIntWidth(op->getType())`
  (`arithmeticOverflow`). Widths `0` or `> 64` are skipped entirely.
- Both operands must be compile-time-known (`knownConst`). A runtime-variable
  overflow is **not** rejected at compile time (see §7).
- Division/remainder checks apply to integer `/` and `%` regardless of
  signedness; only the *overflow* rule is restricted to signed integer types
  (all in `VisitBinaryOperator`).

---

## 2. `CPLUS-ARITH` diagnostics: exact strings and categorization

`diagnosticCategory()` assigns the category by message substring: any message
containing `overflow`, `division by zero`, or `remainder by zero` becomes
`CPLUS-ARITH`. `report()` prefixes `[CPLUS-ARITH] ` at `DiagLevel::Error`;
`warnAt()` prefixes the same tag at `DiagLevel::Warning` in DEFAULT mode.

Caveat: categorization is substring-based, so *other* checker messages that
happen to contain the word "overflow" (for example an allocation-size wrap
message) also receive the `[CPLUS-ARITH]` tag. The tag alone does not identify
which rule fired.

Exact messages observed:

```
[CPLUS-ARITH] signed integer overflow in addition
[CPLUS-ARITH] signed integer overflow in subtraction
[CPLUS-ARITH] signed integer overflow in multiplication
[CPLUS-ARITH] signed integer overflow in call to '<helper-name>'
[CPLUS-ARITH] division by zero
[CPLUS-ARITH] remainder by zero
[CPLUS-ARITH] division by zero cannot be excluded
[CPLUS-ARITH] remainder by zero cannot be excluded
```

`docs/SAFE_SUBSET.md` (§"Compile-time rejected") and `docs/RUST_COMPARISON.md`
name the same category.

---

## 3. DEFAULT vs STRICT

| Mode | Arithmetic finding | `check` exit | `build` result |
| --- | --- | --- | --- |
| `--strict` | `report(...)` → `error: [CPLUS-ARITH] ...` | non-zero (1) | fails, no binary |
| `--default` | `warnAt(...)` → `warning: [CPLUS-ARITH] ...` | 0, prints `check: <file> ok` | succeeds |

The message text is identical; only severity and exit status differ. The
arithmetic rules are among the checks gated on `strict_`; other C+ checks (bounds,
null, ownership, etc.) report unconditionally in both modes.

Observed on the user reproduction:

```text
# c+ check --strict scratch/demo.cp
scratch/demo.cp:19:12: error: [CPLUS-ARITH] division by zero cannot be excluded
scratch/demo.cp:30:31: error: [CPLUS-ARITH] signed integer overflow in call to 'safe_add'
scratch/demo.cp:37:12: error: [CPLUS-ARITH] signed integer overflow in call to 'safe_subtract'
scratch/demo.cp:44:12: error: [CPLUS-ARITH] signed integer overflow in call to 'safe_multiply'
EXIT=1

# c+ check --default scratch/demo.cp
scratch/demo.cp:19:12: warning: [CPLUS-ARITH] division by zero cannot be excluded
scratch/demo.cp:30:31: warning: [CPLUS-ARITH] signed integer overflow in call to 'safe_add'
scratch/demo.cp:37:12: warning: [CPLUS-ARITH] signed integer overflow in call to 'safe_subtract'
scratch/demo.cp:44:12: warning: [CPLUS-ARITH] signed integer overflow in call to 'safe_multiply'
check: scratch/demo.cp ok
EXIT=0
```

`c+ build --strict --sanitize=none scratch/demo.cp -o demo.bin` emits the same
four `error:` lines as `check --strict` and fails to produce a binary. This is
why the arithmetic suite asserts that check and build agree without sanitizers.

---

## 4. The three user reproductions and observed diagnostics

### Reproduction 1 — direct signed overflow

```c
#include <limits.h>
int main(void){int v=INT_MAX;return v+1;}
```

`c+ check --strict`:

```text
<file>:2:37: error: [CPLUS-ARITH] signed integer overflow in addition
  int main(void){int v=INT_MAX;return v+1;}
                                      ^
EXIT=1
```

The sibling cases in the suite cover the other two operators and produce the
same category with different text:

```c
int main(void){int v=INT_MIN;return v-1;}   /* signed integer overflow in subtraction */
int main(void){int a=100000;return a*a;}    /* signed integer overflow in multiplication */
```

### Reproduction 2 — compile-time divide/modulo by zero and unproven divisors

```c
int main(void){int z=0;return 1/z;}   /* division by zero */
int main(void){int z=0;return 1%z;}   /* remainder by zero */
```

Observed with `c+ check --strict`:

```text
<file>:1:31: error: [CPLUS-ARITH] division by zero
  int main(void){int z=0;return 1/z;}
                                ^
<file>:1:31: error: [CPLUS-ARITH] remainder by zero
  int main(void){int z=0;return 1%z;}
                                ^
```

When the divisor is not a known constant and no non-zero proof exists, the word
is "cannot be excluded":

```c
int divide(int n,int d){return n/d;}
int main(int argc,char**argv){(void)argv;return divide(100,argc-1);}
```

```text
<file>:1:32: error: [CPLUS-ARITH] division by zero cannot be excluded
  int divide(int n,int d){return n/d;}
                                 ^
```

### Reproduction 3 — the exact user `scratch/demo.cp` (volatile locals + tiny helpers)

This is the reproduction recorded in commit `cbb6e3d`. The program passes
`volatile` integer constants into three tiny helpers, so a direct-expression
check cannot see either the operation or its operands:

```c
static int safe_add(int left, int right)      { return left + right; }
static int safe_subtract(int left, int right) { return left - right; }
static int safe_multiply(int left, int right) { return left * right; }
static int safe_divide(int numerator, int denominator) { return numerator / denominator; }

volatile int add_left = INT_MAX;        volatile int add_right = 1;
volatile int subtract_left = INT_MIN;   volatile int subtract_right = 1;
volatile int multiply_left = INT_MAX;   volatile int multiply_right = 2;
volatile int numerator = 100;           volatile int denominator = argc - 1;

safe_add(add_left, add_right);                    /* 1. Signed addition overflow */
safe_subtract(subtract_left, subtract_right);    /* 2. Signed subtraction underflow */
safe_multiply(multiply_left, multiply_right);     /* 3. Signed multiplication overflow */
safe_divide(numerator, denominator);              /* 4. Runtime division by zero */
```

The `volatile` declarations above are locals inside `main` in the real file;
they are shown together here for brevity. `c+ check --strict scratch/demo.cp` /
`c+ build --strict --sanitize=none` observed
diagnostics (verbatim, including source locations):

```text
scratch/demo.cp:19:12: error: [CPLUS-ARITH] division by zero cannot be excluded
      return numerator / denominator;
             ^~~~~~~~~
scratch/demo.cp:30:31: error: [CPLUS-ARITH] signed integer overflow in call to 'safe_add'
      printf("   Result: %d\n", safe_add(add_left, add_right));
                                ^~~~~~~~
scratch/demo.cp:37:12: error: [CPLUS-ARITH] signed integer overflow in call to 'safe_subtract'
             safe_subtract(subtract_left, subtract_right));
             ^~~~~~~~~~~~~
scratch/demo.cp:44:12: error: [CPLUS-ARITH] signed integer overflow in call to 'safe_multiply'
             safe_multiply(multiply_left, multiply_right));
             ^~~~~~~~~~~~~
```

The same four lines are emitted as `warning:` by `check --default`, which then
exits 0. Sections 5 and 6 of the same demo (safe boundaries such as
`INT_MAX-1 + 1`, and unsigned `UINT_MAX + 1U`) produce no diagnostic and are
accepted.

---

## 5. How each rule is implemented

### 5.1 Signed add/sub/mul overflow

`VisitBinaryOperator` restricts the rule to
`op->getType()->isSignedIntegerType()` and opcodes `BO_Add`/`BO_Sub`/`BO_Mul`.
Both operands must satisfy `knownConst` (Clang `EvaluateAsInt`, or a tracked
local/field constant in `constLocals_`). For signed expressions the stored bit
pattern is reinterpreted with `getSExtValue()` even if Clang folded it into an
unsigned `APSInt` (the `asLL` lambda). `arithmeticOverflow` then uses `__int128`
to detect the out-of-range result before wrapping.

### 5.2 Compile-time divide/modulo by zero

Same visitor (`VisitBinaryOperator`): if `knownConst(RHS)` yields zero, the message is
`division by zero` for `BO_Div` and `remainder by zero` for `BO_Rem`.

### 5.3 Unproven runtime divisors

`provablyNonZero` accepts: a known non-zero constant; a variable in
`nonZeroVars_`, populated for a bare integer `if (d)` or `while (d)` condition
(`TraverseIfStmt` / `TraverseWhileStmt`); a `for` induction variable whose
recorded lower bound is `> 0` (`TraverseForStmt`); or such a counter plus a
non-negative known offset. Anything else yields `division by zero cannot be
excluded` / `remainder by zero cannot be excluded` in STRICT.

### 5.4 Inferred helper effects

`ArithEffect` is `{op, lhs-index, rhs-index, width}`. In the collection pass,
`TraverseFunctionDecl` scans a function body for a `return` whose value is a
signed `BO_Add`/`BO_Sub`/`BO_Mul` of two *distinct direct parameters* and records
the effect by function name. The effects are wired into the checking visitor via
`setArithEffects` in `checkAstSafety`. At a direct call, `VisitCallExpr` evaluates the
two operand arguments with `knownConst` and applies the same width-aware
`arithmeticOverflow`, emitting `signed integer overflow in call to '<name>'`.
This is what makes reproductions 3's three calls fail: no annotation or general
interprocedural analysis is required.

### 5.5 Volatile local constants

Commit `cbb6e3d` removed the `!vd->getType().isVolatileQualified()` guard from
the collection of constant integer-local initializers in `VisitVarDecl`, so
`volatile int a = INT_MAX;` is now a compile-time constant for these checks.
The value is still discarded once the base name appears in `assignedVars_`
(`knownConst`), i.e. after reassignment. Constant *struct-field* aggregation
separately still excludes volatile-qualified aggregates (also `VisitVarDecl`).

### 5.6 Unsigned modulo acceptance

Overflow detection is gated on signed integer types, so `unsigned` add/multiply
wrap-around is accepted (`unsigned_wrap`, `unsigned_mul`, and demo.cp §6). This
matches the defined C modulo behaviour. Division/remainder-by-zero rules still
apply to unsigned divisors, and overflowing unsigned constants used as an
allocation size are handled by the separate size-wrap rule that was narrowed in
`7e44f81` to fire only when the wrapped value is actually used as a size.

---

## 6. Exact regression suite commands

From the repository root, with a configured and built `build/c+` (CI configure:
`cmake -S . -B build -G Ninja -DLLVM_DIR=... -DClang_DIR=...
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then `cmake --build build --target c+
--parallel`):

```sh
# 1. Dedicated arithmetic suite (15 cases). Each case runs:
#      c+ check --strict <src>
#      c+ build --strict --sanitize=none <src> -o <exe>
#      c+ check --default <src>
CPLUS_CLANG=/usr/bin/clang python3 tests/core/arithmetic_safety_suite.py
# observed: "arithmetic suite: 15/15 passed (check --strict, build --strict
#           --sanitize=none, default)"; report written to
#           tests/out/core/arithmetic/report.txt

# 2. Full core safety runner (invokes arithmetic_safety_suite.py,
#    rust_compare_suite.py, strict_mode.sh, sanitizer/mode/fuzz suites, ...).
CPLUS_BIN="$PWD/build/c+" sh tests/core/run.sh

# 3. Paired C+/Rust compile-time comparison (arith_add/sub/mul,
#    arith_div_const, arith_div_unknown, arith_helper, arith_safe, ...).
CPLUS_BIN="$PWD/build/c+" python3 tests/core/rust_compare_suite.py
# observed: "rust-compare suite: 27/27 C+ expectations met"
#           "compile-time rejection: C+ 22/27, Rust 19/27"

# 4. CTest wrappers around repo tests and demos.
ctest --test-dir build --output-on-failure

# 5. Full verification driver (build, ctest, fuzz, metrics, mode suites, ...).
./tools/verify.sh
```

The arithmetic suite's 15 cases are: `int_max_add`, `int_min_sub`,
`mul_overflow`, `runtime_div_zero`, `const_div_zero`, `const_mod_zero`,
`safe_boundary`, `safe_div`, `guarded_div`, `unsigned_wrap`, `unsigned_mul`,
`helper_add_overflow`, `helper_sub_overflow`, `helper_mul_overflow`,
`helper_safe_add` (source: `tests/core/arithmetic_safety_suite.py`).

---

## 7. Honest remaining gaps

These are verified limitations of the current implementation, not claims of
completeness:

1. **Runtime-variable overflow is not rejected at compile time.** The overflow
   rule requires both operands to be `knownConst`. Verified: with
   `int x = argc ? INT_MAX : 0; return x + 1;` and with a helper `add(x, 2)`,
   `c+ check --strict` exits 0 with "ok". Such cases are left to the optional
   sanitizer at runtime (or go undetected with `--sanitize=none`). This is the
   central difference from a full static overflow prover.

2. **Nested helpers are not inferred.** The effect inference recognizes only a
   single `return a OP b;` on two direct parameters. Verified: a helper
   `add3(x,y,z){ return add(x,y)+z; }` called with volatile `INT_MAX, 1, 1`
   arguments is accepted (no finding at the `add3` call site). Inference also
   does not handle multi-return bodies, constants mixed with parameters,
   casts, or interprocedural chains. (An additional name-collision quirk was
   observed: because `constLocals_` is keyed by variable *name*, helper
   parameters named like a `volatile` local in `main`, e.g. `a`/`b`, can resolve
   to that local's value and produce a false-positive finding. The 15-case
   suite uses distinct `left`/`right` parameter names and does not cover this.)

3. **Narrow non-zero proofs; possible false positives.** Only a bare integer
   condition `if (d)` / `while (d)` registers `d` in `nonZeroVars_`. Verified:
   the provably safe `if (d != 0) return n / d;` is still rejected in STRICT
   with `division by zero cannot be excluded`. `TraverseDoStmt` does not add a
   guard variable either. There is no general value-range analysis.

4. **Volatile-constant tracking is name- and straight-line-based.** A
   `volatile` local's initializer is treated as constant for these checks;
   reassignment by name invalidates it, but there is no path sensitivity, and
   address-taken/aliased writes are not modelled for this purpose.

5. **No sanitizer dependence — and therefore no runtime backstop without one.**
   The compile-time verdicts are identical with `--sanitize=none`. Conversely,
   enabling UBSan is the only mechanism for the runtime-variable cases in gap 1;
   the checker does not emit runtime guards for arithmetic.

6. **Rust differences are real and recorded, not smoothed over.** From
   `docs/RUST_COMPARISON.md` and a fresh run of `rust_compare_suite.py`:
   - Direct constant overflow and `1/0`: both C+ and Rust reject (Rust via
     const-eval).
   - Runtime-unknown divisor: C+ STRICT rejects (`division by zero cannot be
     excluded`), Rust accepts because a plain `i32` divisor may be zero and
     fails as a runtime panic. C+ is stricter here.
   - Non-const helper with constant arguments: C+ infers the effect and
     rejects; Rust accepts and panics at runtime in debug builds. C+ is
     stricter here.
   - Aggregate result: C+ 22/27 and Rust 19/27 compile-time rejections; 17
     scenarios agree. This is a paired, overlapping-subset measurement, **not**
     parity with Rust's ownership model.

7. **The category is lexical.** Any message containing "overflow", "division by
   zero", or "remainder by zero" is tagged `[CPLUS-ARITH]`; the tag is not a
   proof that the signed-overflow rule specifically fired.

---

## 8. Evidence index

- Implementation: `compiler/Sema/AstSafetyCheck.cpp` — rules in `knownConst`,
  `provablyNonZero`, `arithmeticOverflow`, `TraverseFunctionDecl`,
  `TraverseIfStmt` / `TraverseWhileStmt` / `TraverseForStmt`, `VisitVarDecl`,
  `VisitBinaryOperator`, `VisitCallExpr`, `warnAt`, `diagnosticCategory`,
  `report`, and `checkAstSafety`.
- Tests: `tests/core/arithmetic_safety_suite.py`,
  `tests/core/rust_compare_suite.py`, `tests/core/run.sh`,
  `tests/safety/rust_compare/{c,rs}/arith_*.{c,rs}`.
- Reproduction source: `scratch/demo.cp`.
- Related docs: `docs/SAFE_SUBSET.md`, `docs/RUST_COMPARISON.md`,
  `docs/SAFETY_COVERAGE.md`, `docs/CRITICAL_ISSUES.md`, `docs/LIMITATIONS.md`.
- Commits: `7e44f81` (initial CPLUS-ARITH analysis) and `cbb6e3d` (volatile
  locals, inferred helper effects, demo.cp coverage).

## Native test path (no Python)

The active compiler-safety unit tests are now C+ programs under `tests/native/`, built by the C+ driver: `cplus_safety_tests.c` (36 exact-diagnostic paired cases) and `strict40_runner.c` (the authoritative 40-test STRICT suite). `tests/core/run.sh` no longer invokes any Python test harness.

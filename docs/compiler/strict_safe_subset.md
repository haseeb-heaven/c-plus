# Strict-safe C subset — specification

**Version:** v0.1 (2026-11-24) · **Status:** normative for the `STRICT` compiler
mode; evidence-backed by `tests/compiler/strict_rules/` and the generated
`docs/compiler/strict_rules_matrix.json`.

This document defines what the compiler *guarantees* in `STRICT` mode. It does
not define a new language: it is a restriction on C+ source plus a small set of
runtime helpers. Every rule below is stated as exactly one of:

- **Static proof rule (S)** — the compiler accepts the operation only when the
  property is provable from the translation unit's types, extents, ownership
  state and control-flow facts. Unverified operations are rejected (fail
  closed).
- **Runtime check (R)** — no static proof is claimed; emitted code checks the
  property at run time and aborts with a diagnostic on violation.
- **Trusted boundary (T)** — the unsafe plain-C form is rejected at the source
  boundary; safety is delegated to a named C+ helper/library whose contract is
  trusted, not proved by this compiler.

## 1. Guarantee and its limits

In `STRICT` (`c+ check --strict` / `c+ build --strict`) a program admitted by
all rules in section 3 has, for each listed category, a proof, a runtime guard,
or a trusted boundary covering that category. `DEFAULT` and `LEGACY` are **not**
guaranteed modes (section 4).

**No Rust-level soundness is claimed.** The plain-C ABI remains the FFI and
calling boundary: externally supplied pointers, variadic calls, signal
handlers, concurrency primitives and hand-written assembly are outside the
proof unless they cross one of the trusted boundaries named below. In
particular this subset does not prove absence of data races in arbitrary
third-party threads, does not type-check prefabricated C object files, and
does not extend the runtime's reachability analysis across translation units
(STRICT emits a `CPLUS-MULTITU` per-TU warning where that matters). "Safe"
here means: the listed failure is rejected, guarded, or confined — not that
every C program is memory-safe.

## 2. Diagnostic contract

STRICT turns proven violations into **errors** (nonzero exit). The stable tag
families are `CPLUS-ARITH`, `CPLUS-BOUNDS` / `CPLUS-SAFE-BOUNDS`,
`CPLUS-LIFETIME` / `CPLUS-SAFE-LIFETIME`, `CPLUS-OWNER`, `CPLUS-FFI`,
`CPLUS-REALLOC`, `CPLUS-ALIAS`, `CPLUS-MULTITU`. Some findings come from the C
backend under `-Werror` (for example definite-assignment and
return-stack-address); those are pinned by substring in the suite exactly like
the C+ tags. Known tag-shape inconsistencies are documented in
`docs/compiler/safety-diagnostics.md` and are not silently renamed.

## 3. Rules by operation category

Rule IDs in backticks are the keys of the coverage matrix. Each entry states
the mechanism that makes the operation safe.

### 3.1 Arithmetic

- `ARITH-OVERFLOW` **(S)**: a signed add/sub/mul is admitted only if every
  operand's static range excludes overflow; otherwise STRICT rejects with
  `signed integer overflow in addition`.
- `ARITH-DIVZERO` **(S)**: division/modulo requires a divisor provably nonzero;
  a provably zero divisor is rejected with `division by zero`. Dynamic divisors
  are not statically admitted by this rule and rely on the runtime arithmetic
  guard; the suite verifies the static half only.

### 3.2 Fixed, heap and VLA bounds

- `BOUNDS-FIXED` **(S)**: constant index `i` into fixed array length `N` is
  admitted only if `0 <= i < N`, else `outside the bounds`.
- `BOUNDS-HEAP` **(S)**: for a tracked allocation of `S` bytes passed into a
  callee, each `p[i]` must be provable inside `S / sizeof(*p)`; interprocedural
  extent propagation rejects the violation (`exceeds heap allocation`).
- `BOUNDS-VLA` **(T)**: variable-length extents are not part of the plain-C ABI
  contract accepted here. Fixed extents or `cplus`-managed buffers with an
  explicit length field are the admitted forms; a `sizeof` whose VLA operand
  has side effects is rejected.
- `BOUNDS-DYNAMIC-RUNTIME` **(R)**: where no extent proof exists, the emitted
  C+ bounds guard checks the index against the tracked allocation length and
  aborts with `C+ error: index N is outside the bounds of 'p' (length L)`.
  This is the one rule in this version verified by build-and-run instead of
  static check.

### 3.3 Null and uninitialized state

- `NULL-DEREF` **(S)**: a pointer known null on any reaching path may not be
  dereferenced; the guard must dominate the dereference.
- `UNINIT-JOIN` **(S)**: definite-assignment CFG analysis requires a value on
  every path reaching a read; a join with an unassigned arm is rejected.

### 3.4 Aliasing and provenance

- `ALIAS-TYPEPUN` **(S)**: dereference through an lvalue incompatible with the
  stored object's effective type is rejected (`type-punned dereference`); byte
  copies via `memcpy` are the admitted reinterpretation path.
- `PROVENANCE-ESCAPE` **(S)**: a managed or borrowed pointer may not outlive its
  owner's scope; returning the pointee by value is admitted, returning the
  managed pointer is rejected.

### 3.5 Lifetime, free and realloc

- `LIFETIME-UAF` **(S)**: after a tracked owner is released, a liveness check
  or dereference of the same pointer is rejected (`use-after-free`).
- `LIFETIME-DOUBLE-FREE` **(S)**: each tracked owner is released exactly once
  per path; a second release is rejected (`double free`).
- `LIFETIME-REALLOC` **(T)**: raw `realloc` is rejected because failure loses
  the original block. `cplus_realloc` / `cplus_realloc_registered` with an
  explicit success check are the trusted resize boundary.

### 3.6 Strings and FFI

- `FFI-FORMAT` **(S/T)**: the format argument of the `printf` family must be a
  string literal; non-literal format strings are rejected. Untrusted text is
  the trusted-boundary payload, passed only as a `%s` argument to a literal
  format. The same boundary applies to other checked libc contracts (`%n`,
  unbounded `%s` scans, overlap checks for `memcpy`).

### 3.7 RAII and callbacks

- `RAII-DEFER-JUMP` **(S)**: `return`/`goto`/`break` may not leave a
  `defer`/resource cleanup block, so cleanup always executes.
- `RAII-SIGNAL-CALLBACK` **(S/T)**: a handler body may call only
  async-signal-safe functions (`not async-signal-safe` otherwise); handler
  state is restricted to `volatile sig_atomic_t`, and all other work is
  deferred out of the callback.

### 3.8 Control-flow joins

- `JOIN-PHI-BOUNDS` **(S)**: at a phi/conditional expression the worst-case
  index across all arms must lie in the callee's proven extent; a single
  out-of-range arm is rejected (`chosen by conditional expression is out of
  bounds`). This rule and `UNINIT-JOIN` are the join guarantees.

### 3.9 Threads

- `THREAD-RACE` **(S/T)**: a thread function may not write a shared global
  without synchronization (`unsynchronized write to shared global`). Shared
  mutable state must be thread-confined or cross an explicit synchronization
  primitive; the compiler does not independently prove third-party mutex
  implementations.

### 3.10 Function boundaries and fields/containers

- `FUNC-BOUNDARY-NULL` **(S)**: at a call site, an argument proven null cannot
  be passed to a callee that dereferences it (`null pointer argument ...`).
- `FIELD-RESOURCE-COPY` **(S)**: a `resource`-typed field/container object is
  linear; it may be passed by address but not copied by value into a second
  owner (`cannot be copied`).

## 4. Mode semantics (non-guaranteed modes)

- `STRICT` is the only mode to which section 3 applies. Unverified operations
  are errors; this is the fail-closed contract.
- `DEFAULT` reports proven errors but **warns** on unverified operations and
  admits them. It is a diagnostic/transition mode: acceptance in DEFAULT is
  **not** evidence of safety.
- `LEGACY` (`--legacy-c`) is a compatibility mode explicitly **not memory
  safe**; it exists to compile existing C and must not be used as evidence for
  any rule here.
- Sanitizer choice does not change these semantics. `--sanitize=none` still
  emits C+ runtime guards; sanitizers are additional detectors, not the proof.

## 5. Verification protocol and evidence

The permanent suite is `tests/compiler/strict_rules/`:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/strict_rules/run_strict_rules.py
```

For every rule in `rules.json` it executes a POSITIVE fixture (must exit 0 and
contain none of that rule's forbidden substrings) and a NEGATIVE fixture (must
exit nonzero and contain the required diagnostic substring). All commands run
with a hard timeout (default 45 s, `CPLUS_STRICT_TIMEOUT` override); timeouts
are FAILs. There are no skips and no `|| true`. `BOUNDS-DYNAMIC-RUNTIME` uses
`build` + execute to exercise its runtime guard; all other rules use `check`.

The runner writes full evidence to `results/summary.json` / `summary.txt` and
**generates** `docs/compiler/strict_rules_matrix.json` from those measurements.
A rule's matrix `status` is `VERIFIED` only if both of its fixtures actually
ran within the timeout and met their assertions; otherwise `UNVERIFIED`.
`false_positives` and `false_negatives` are separate top-level JSON fields with
concrete fixture names and observed exit codes. Coverage is never inferred
from source inspection alone.

## 6. Known limits of v0.1

- STRICT analysis is per translation unit; cross-TU data and control flow are
  not safety-checked (emitted as a `CPLUS-MULTITU` warning).
- Runtime bounds evidence covers the tracked-allocation model only; arbitrary
  external pointers crossing the plain-C ABI are trusted, not proved.
- Backend-derived diagnostics (`-Werror` findings) are version-sensitive; the
  suite pins substrings, not whole lines or source coordinates.
- Rules whose only mechanism is a trusted boundary are verified by rejecting
  the raw form and accepting the helper form; the helper's implementation is
  outside this compiler's proof.

## 7. Versioning

v0.1 introduces the 20 rule IDs, the three-mechanism vocabulary, the mode
semantics and the execution-derived matrix. Additions require a new version,
new positive/negative fixtures, and a regenerated matrix.

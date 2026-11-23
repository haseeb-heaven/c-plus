# Checked-CRT memory examples — Rust equivalence comparison

Branch: `agent/modern-stdlib` · Run date: 2026-11-23 (UTC) · Host: Darwin 27.2.0 arm64

Scope: the 9 checked-CRT memory examples in `examples/crt/`:
`malloc_s.c`, `calloc_s.c`, `realloc_s.c`, `free_s.c`, `memcpy_s.c`,
`memmove_s.c`, `memset_s.c`, `memzero_s.c`, `memcmp_s.c`.

Deliverables in this directory:

- `<name>.rs` — one standalone **safe** Rust equivalent per example (no `unsafe`, no FFI).
- `compare.sh` — shared build/run/diff driver; prints one `EXAMPLE_COMPARE …` line per example plus totals.
- `cbuild/`, `rbuild/` — compiled binaries and captured stdout/stderr/exit evidence.
- `RESULTS.md` — this report.

No files outside this directory were created or modified. No `git add`/commit was performed. `examples/` and `stdlib/` were not edited.

---

## 1. Exact commands and toolchains

### Toolchains (exact)

| Tool | Version |
|---|---|
| C compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin27.2.0` |
| Rust compiler | `rustc 1.100.0 (0ef1bc386 2026-11-09)` (cargo `1.100.0 (3acc79c0c 2026-11-05)`) |
| OS/kernel | Darwin 27.2.0, Darwin Kernel Version 27.2.0, root:xnu-13432.41.4~25/RELEASE_ARM64_T8112, arm64 |

### Libraries linked (exact, with SHA-256)

| Archive | Size | SHA-256 |
|---|---|---|
| `build/safe/libcplus_safecrt.a` | 458520 B | `ac66b0246dbac3337f439e4b91b0e50f901aac7f07bba0ecb1581c637187ef2e` |
| `build/libc/libcplus_hardened.a` | 295592 B | `86f6a7223f19d10a3e8b1d9b9902731ba05fc061e7ce1463c08a3047281c5dda` |

### C compile command

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
/usr/bin/clang -std=c99 \
  -Istdlib/safe -Ilibc/install-include -Ithird_party/safeclib/include \
  examples/crt/<name>.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -o benchmarks/example_rust_compare/crt_memory/cbuild/<name>
```

**Documented deviation from the supplied command line:** the task specified
`-Ilibc/install-include -Ithird_party/safeclib/include`. Those two include
paths alone are insufficient — `safe_alloc.h` and `safe_memory.h` are not
installed under `libc/install-include`; they exist only at
`stdlib/safe/safe_alloc.h` and `stdlib/safe/safe_memory.h`. Without
`-Istdlib/safe` every one of the 9 examples fails with
`fatal error: 'safe_alloc.h' file not found` / `'safe_memory.h' file not found`
(verified). `-Istdlib/safe` was therefore added; all other flags and both
archives are exactly as specified.

### Rust compile command

```sh
rustc -O --edition 2021 benchmarks/example_rust_compare/crt_memory/<name>.rs \
  -o benchmarks/example_rust_compare/crt_memory/rbuild/<name>
```

(`--edition 2021` is explicit; `rustc -O` alone also compiles these files.)

### Run/compare

```sh
bash benchmarks/example_rust_compare/crt_memory/compare.sh
```

Each binary runs as `timeout 30 <binary>`; stdout bytes (`cmp -s`) and exit
status are compared. `CPLUS_RUST_TRACE=1` additionally produces
`rbuild/<name>.trace.stdout` evidence for the modeled refusal cases (not part
of the MATCH verdict).

---

## 2. Method

For each example:

1. Read the C source and the implementation contracts in
   `stdlib/safe/safe_memory.h`, `stdlib/safe/safe_alloc.h`,
   `stdlib/safe/src/safe_memory.c`, `stdlib/safe/src/safe_alloc.c`.
2. Probe the real runtime with two purpose-built C probes (`cbuild/probe.c`,
   `cbuild/probe_alloc.c`) to pin down exact errno codes, constraint codes,
   and destination-buffer side effects — see §5.
3. Write a safe-Rust equivalent that reproduces the example's **valid case**
   byte-for-byte on stdout and models the example's **refusal case(s)** with
   `Option`/`Result` (never `unsafe`, never FFI).
4. Compile both, run both under `timeout 30`, diff stdout bytes + exit status.

Refusal cases are *executed and asserted* by every Rust program. By default
they are silent so stdout matches the C program; setting `CPLUS_RUST_TRACE=1`
prints a `[rust-checked] …` line per refusal case (captured in `rbuild/`).

---

## 3. Per-example results

| Example | C stdout (bytes) | Rust stdout | Exit (C/R) | Valid case | Refusal case(s) modeled in Rust | Verdict |
|---|---|---|---|---|---|---|
| `malloc_s` | `malloc_s: ok\n` (13) | identical | 0 / 0 | 1024-byte allocation then free | oversize → `None` (C EINVAL 22); size 0 → 1-byte block; double free → second `free_s` returns `false`, no UB | **MATCH** |
| `calloc_s` | `calloc_s: ok\n` (13) | identical | 0 / 0 | `calloc_s(4,16)`, all 64 bytes zero | `checked_mul` overflow → `None` (C EOVERFLOW 84); `(0,0)` → 1 zeroed byte; double free refused | **MATCH** |
| `realloc_s` | `realloc_s: ok\n` (14) | identical | 0 / 0 | 4→64 byte realloc preserves `"abc\0"`, `strcmp` passes | oversize → `Err(original)` with original untouched (C EINVAL 22); `NULL` ptr → fresh allocation | **MATCH** |
| `free_s` | `free_s: ok\n` (11) | identical | 0 / 0 | alloc, free, then a second (refused) free | `free_s(NULL)` no-op; double free refused (C errno 22) with no UB | **MATCH** |
| `memcpy_s` | 73 B: `IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max\n` + `memcpy_s: ok\n` | identical (73 B) | 0 / 0 | `dmax=16, smax=8` copy of `"C+ safe\0"`, `strcmp` passes | exercised refusal `dmax=3, smax=8` → `Err(NoSpace)` (406), no overflow; source first 3 dest bytes zeroed like safeclib | **MATCH** |
| `memmove_s` | `memmove_s: ok\n` (14) | identical | 0 / 0 | overlapping copy `buf+2 ← buf, n=3` yields `"ababc"` via `copy_within` | dest range past end → `Err(NoSpace)` (406) | **MATCH** |
| `memset_s` | 71 B: `IGNORE CONSTRAINT HANDLER: (406) memset_s: n exceeds dmax\nmemset_s: ok\n` | identical (71 B) | 0 / 0 | fill 8 bytes with `0xAA` | exercised refusal `dmax=4, n=8` → `Err(NoSpace)` (406); bytes past `dmax` unchanged; first 4 zeroed like safeclib | **MATCH** |
| `memzero_s` | `memzero_s: ok\n` (14) | identical | 0 / 0 | zero 8-byte `"secret"` | zero length → `Err(ZeroLength)` (C ESZEROL 401, defined refusal); oversize → `Err(TooLarge)` | **MATCH** |
| `memcmp_s` | `memcmp_s: ok\n` (13) | identical | 0 / 0 | `diff=0` for abc/abc; `diff<0` for abc/abd | zero length → `Err(ZeroLength)` (401); ordering returned as `Ok(-1)` instead of `diff` out-param | **MATCH** |

All builds produced empty build logs (no warnings/errors); all captured stderr files were empty; all runs exited 0 within 30 s.

### Verdict totals

```
EXAMPLE_COMPARE TOTAL MATCH=9 DIFFERENT=0 BLOCKED=0
```

- **MATCH: 9 / 9**
- **DIFFERENT: 0**
- **BLOCKED: 0** (no case was untestable; both toolchains and both archives were available, all 9 programs compiled and ran)

### Verbatim driver output

```
EXAMPLE_COMPARE malloc_s MATCH stdout=13B exit=0; alloc failure/double-free via Option/Result
EXAMPLE_COMPARE calloc_s MATCH stdout=13B exit=0; alloc failure/double-free via Option/Result
EXAMPLE_COMPARE realloc_s MATCH stdout=14B exit=0; alloc failure/double-free via Option/Result
EXAMPLE_COMPARE free_s MATCH stdout=11B exit=0; alloc failure/double-free via Option/Result
EXAMPLE_COMPARE memcpy_s MATCH stdout=73B exit=0; stdout incl. mirrored safeclib 406 handler line; refusal = Err(NoSpace)
EXAMPLE_COMPARE memmove_s MATCH stdout=14B exit=0; overlap via copy_within; aliasing modelled as slice offsets
EXAMPLE_COMPARE memset_s MATCH stdout=71B exit=0; stdout incl. mirrored safeclib 406 handler line; refusal = Err(NoSpace)
EXAMPLE_COMPARE memzero_s MATCH stdout=14B exit=0; zeroing via fill(0); zero-length = Err
EXAMPLE_COMPARE memcmp_s MATCH stdout=13B exit=0; ordering via return i32 (C uses diff out-param)
EXAMPLE_COMPARE TOTAL MATCH=9 DIFFERENT=0 BLOCKED=0
```

---

## 4. Non-equivalence statement — MATCH here means *observable stdout/exit*, not semantic identity

**The C+ checked-CRT runtime and the safe-Rust equivalents are NOT semantically equivalent.** The 9/9 MATCH verdict is strictly about the compared observable surface (stdout bytes and process exit status for the example's valid path, with the refusal cases asserted internally). The underlying failure models are fundamentally different:

- **C+ (runtime):** violations are detected at run time by safeclib constraint checks and the tracked-allocation table. Failures are reported through positive safeclib constraint codes (`ESNULLP` 400, `ESZEROL` 401, `ESLEMAX` 403, `ESOVRLP` 404, `ESNOSPC` 406) returned as `errno_t`, and/or through `errno` (`EINVAL` 22, `EOVERFLOW` 84, `ENOMEM` 12). Violations also fire a **constraint handler that writes diagnostics to stdout**, and safeclib **zeroes the first `dmax` destination bytes** on `ESNOSPC`.
- **Rust (compile-time / checked returns):** invalid pointers, dangling pointers and double-free are largely *unrepresentable* under ownership and the borrow checker; bounds are encoded in slice lengths; failures are ordinary `Option`/`Result` values. Rust has **no `errno`, no constraint handler, and no runtime violation callback**. Where the C program's stdout contains a constraint-handler line, the Rust program **emits that exact line explicitly with `println!`** solely to mirror the observable output — there is no handler in Rust. The refusal itself is a returned `Err`.
- **Allocation:** C `malloc_s` may return `NULL` with `errno=ENOMEM`; Rust `Vec::try_reserve_exact` models that as `None`. Rust `Vec` always initialises memory (zeros here), while C `malloc_s` leaves bytes indeterminate. C tracks allocation metadata at runtime; Rust gets lifetime safety from the type system instead.
- **Overlap:** C `memmove_s` accepts arbitrary overlapping raw pointers; safe Rust cannot even form overlapping `&mut`/`&` aliases, so the example is modelled with `slice::copy_within` on one slice (offsets instead of pointers).
- **memcmp:** C writes the ordering through an `int *diff` out-parameter and returns an error code; Rust returns `Result<i32, CmpErr>`.
- **Secure zeroing:** C `memzero_s` aims at secure erasure; Rust `slice::fill(0)` is not guaranteed to survive optimisation as a volatile wipe (a true secure zero needs `unsafe`/volatile or the `zeroize` crate, which would violate the no-`unsafe` constraint of this task). This is a real fidelity gap, not merely an API difference.

Consequently, these Rust programs are **behavioural models of the examples' observable output**, not drop-in semantic replacements for the C+ runtime's error-reporting machinery. In particular, a caller that branches on `errno_t`/`errno` in C has no equivalent branch here without an explicit mapping layer.

---

## 5. Exact differences / runtime facts established by probing

The two C probes were compiled with the same flags as §1 and run under `timeout 30`. Evidence: `cbuild/probe`, `cbuild/probe_alloc` (sources `cbuild/probe.c`, `cbuild/probe_alloc.c`).

`cbuild/probe` output:

```
cmp equal: r=0 d=0
cmp abc/abd: r=0 d=-1
cmp abd/abc: r=0 d=1
IGNORE CONSTRAINT HANDLER: (400) memzero_s: dest is null
memzero NULL,0: r=400
IGNORE CONSTRAINT HANDLER: (401) memzero_s: dmax is 0
memzero z,0: r=401
IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max
memcpy fail r=406 dst[0]=0 dst[3]=88
IGNORE CONSTRAINT HANDLER: (406) memset_s: n exceeds dmax
memset fail r=406 b[0]=0 b[3]=0 b[4]=170
```

`cbuild/probe_alloc` output:

```
malloc_s SIZE_MAX: p=0x0 errno=22
malloc_s 0: p=0x10479d860 errno=0
calloc_s overflow: p=0x0 errno=84
calloc_s 0,0: p=0x10479d860 errno=0
realloc_s SIZE_MAX: r=0x0 errno=22 q[0]=97
free_checked_s live: rc=0 errno=0
free_checked_s again: rc=22 errno=22
double free_s: errno=22
```

Key consequences used in the models:

| Fact (C+) | Rust model |
|---|---|
| Constraint handler writes to **stdout**, not stderr | Mirrored `println!` lines for `memcpy_s`/`memset_s` (the only two examples that exercise a violation) |
| `ESNOSPC` (406) zeroes first `dmax` dest bytes | `dst.fill(0)` before returning `Err(NoSpace)` — verified `b[4]` stays `0xAA` in `memset_s` |
| `memcmp_s` diff is normal signed ordering (−1/0/1) | `Ok(-1)` etc. |
| `memzero_s(…, 0)` is a defined refusal (401), not a crash | `Err(ZeroLength)` |
| `malloc_s` oversize → NULL, `errno=EINVAL` (22) | `None` |
| `calloc_s` overflow → NULL, `errno=EOVERFLOW` (84) | `None` via `checked_mul` |
| `realloc_s` oversize → NULL, `errno=EINVAL` (22), original untouched | `Err(original)` |
| double `free_s` → refused, `errno=EINVAL` (22), no UB | second `free_s` returns `false` |

There were **no DIFFERENT verdicts**: after mirroring the two observable handler lines, stdout is byte-identical for all 9 examples. The differences above are recorded as required but are mechanism-level, not stdout/exit-level.

---

## 6. Evidence file index

All under `benchmarks/example_rust_compare/crt_memory/`:

```
compare.sh
RESULTS.md
malloc_s.rs  calloc_s.rs  realloc_s.rs  free_s.rs  memcpy_s.rs
memmove_s.rs memset_s.rs  memzero_s.rs  memcmp_s.rs
cbuild/compile_all.sh  cbuild/run_all.sh
cbuild/probe.c  cbuild/probe_alloc.c  cbuild/probe  cbuild/probe_alloc
cbuild/<name>  cbuild/<name>.stdout  cbuild/<name>.stderr  cbuild/<name>.build.log
rbuild/<name>  rbuild/<name>.stdout  rbuild/<name>.stderr  rbuild/<name>.build.log
rbuild/<name>.trace.stdout  rbuild/<name>.trace.stderr
```

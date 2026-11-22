# Adversarial findings and fixes (Agent 2, this branch)

This record is intentionally evidence-first: each item is a defect that an
adversarial test actually triggered, the root cause, the fix, and the
regression evidence. Nothing here is a theoretical or synthetic "bug".

Runners:
- `sh tests/libc/run_attack_tests.sh` — 23 targeted suites (incl. contract integrity)
- `sh tests/libc/run_corpus_tests.sh` — 20 generated bug-class corpora

## Fixed defects

| # | Finding | Root cause | Fix | Evidence |
|---:|---|---|---|---|
| 1 | `cplus_tmpfile()` streams leaked: first `cplus_fclose()` returned EOF/EBADF | `cplus_stdio_unmark_closed()` was only called by `cplus_fopen()`; recycled `FILE*` addresses stayed in the closed-address ring | Added private `stdio_internal.h` with `cplus_stdio_register()`; `tmpfile`, `fdopen`, `freopen` now register their result | `corpus_resource` 700 cases: 301 failures → 0; fd delta +100 → 0 |
| 2 | `cplus_fclose_s()` double-close guard defeated by hash collision | 512-slot single-entry closed/live caches, no collision resolution | Converted both caches to open addressing with linear probing | `test_stdio_attack` 100-file batch: 0 leaks; double-close still refused |
| 3 | `cplus_realloc()` ABA race stripped another thread's live metadata while the host `realloc()` ran with the lock released | Second critical section matched by address only | Capture old `generation` under lock; only act on a slot whose generation still matches | `test_corpus_concurrency` 10,203 ops: 16 metadata-loss failures → 0 across 20 plain + 10 UBSan runs |
| 4 | Real double free misclassified as invalid free after two freed pointers collided | Recent-free cache was a 1-way direct-mapped table | Converted `g_recent` to open addressing | `test_alloc_attack` + `test_corpus_uaf` (700 cases) pass |
| 5 | `cplus_strnlen_s()` read one byte past a 64-byte unterminated source ending at a guard page | Vendored safeclib loop `while (*str && smax)` dereferenced before checking the bound | Deliberate security patch: `while (smax && *str)` in `third_party/safeclib/src/str/strnlen_s.c` | `test_memory_attack`: SIGBUS at `src+64` → clean, 0 findings |
| 6 | `cplus_lroundf`/`cplus_llroundf` returned `float` instead of ISO `long`/`long long`, losing integer range | Wrong return type in header and implementation | Corrected both to ISO types; updated differential test | `test_math_attack` + `test_math_float` pass at `FLT_MAX` |
| 7 | `cplus_frexp(x, NULL)`, `cplus_modf(x, NULL)`, `cplus_remquo(x,y,NULL)` crashed with SIGSEGV in the double family | Missing NULL out-param guards (present only on `*f`/`*l` variants) | Added `errno = EINVAL; return 0.0;` guards | `test_math_attack` forked probes: no child death |
| 8 | `CPLUS_SAFE_OBJ_SIZE` reported 24 bytes for a 16-byte struct member, permitting an 8-byte canary overwrite | `__builtin_object_size(p, 0)` returns bytes to end of enclosing object | Use mode `1` (exact enclosing object) in `safe_memory.h` and `safe_string.h` | `test_format_attack`: canary intact, no FINDING |
| 9 | Contract validator silently accepted corrupted `c17_contracts.json`, empty collections, and failed under `python3 -O` | `assert`-based checks and no C17 file validation | Rewrote validator with explicit `SystemExit` checks, non-empty collection checks, function-declaration matching, path and row-count cross-checks | `test_contract_integrity.py`: 52/52 attacks detected, all cross-checks clean |
| 10 | Logging died permanently after stderr was closed (stale `FILE` error indicator); reentrant `fprintf` from a signal handler killed 4/20 children | `log.c` used `fprintf(stderr, ...)` | Replaced with bounded stack buffer + direct `write(2)`, async-signal-safe and immune to stdio error flags; truncation marker keeps lines well-formed | `test_logging_attack` A4/A5: 0 findings, 23/23 attack suites green |
| 11 | `cplus_alloc_fail_after(-1)` failed every allocation instead of disabling injection | Negative count mapped to `0` (= fail all) | Map negative to `-1` (off) | `test_c17_attack`, failure-injection corpora pass |

## Documented as NOT FIXABLE BY LIBC (no false claim made)

- Cross-allocator free of a `cplus_malloc` pointer with host `free()`: the
  metadata table still reports the address as live because no portable hook
  observes the external free. Caller UB; see `C_BUG_CLASS_MATRIX.md` row 7.
- `cplus_transfer_ownership()` cannot distinguish two caller variables that
  alias the same live pointer; ownership discipline is a caller contract.
- Raw format strings with mismatched varargs and wide `%n` remain unsafe;
  only checked `_s` entry points reject them.
- Async-signal-safety of the *application* code around logging is not
  provided by the logger itself beyond its own `write()`-based path.

## Deterministic scenario volume

| Runner | Generated scenarios |
|---|---:|
| `test_corpus_buffer` | 78,581 |
| `test_corpus_overlap` | 37,164 |
| `test_corpus_integer` | 9,910 |
| `test_corpus_math` | 6,318 |
| `test_corpus_format` | 1,152 |
| `test_differential_attack` | 13,256 |
| `test_corpus_concurrency` | 10,203 operations |
| `test_corpus_{uaf,resource,uninit,math}` | 700 + 700 + 760 cases |

Each corpus uses a fixed seed, defined inputs only, and canaries; raw-C UB is
never executed by a test (it is documented in `FINDING:`/`UNGUARDED:` notes).

## Compile-time bug catalogs

`scratch/bugs4.cp` … `scratch/bugs50.cp` add 470 new, globally unique bug IDs
(305–774) across distinct themes (lifetime, atomics, signals, stdio,
allocators, strings, numeric, FFI contracts, OS resources, C language traps),
each with `BUG`/`SAFE FIX` annotations and a non-calling `main`. Matching
`tests/compile_fail/bugcat4.c` … `bugcat50.c` unit fixtures assert that the C+
compiler rejects at least one representative case per catalog with a
`[CPLUS-` diagnostic. The catalogs record which patterns the current checker
catches and which remain runtime-only or unimplemented — no false
"compiler catches everything" claim.

## Follow-up: ordinary C API hardening (user-reported failure)

| Finding | Root cause | Fix | Evidence |
|---|---|---|---|
| `malloc` + `memset(buffer + offset, ...)` overran the allocation; ordinary calls got no C+ protection | (a) no header mapped ordinary names to the checked layer; (b) tracked-capacity lookup matched only the allocation BASE pointer, so interior pointers were "unknown" and writes were allowed | Added `stdlib/safe/safe_c.h` ordinary-C mapping layer; added `cplus_safe_remaining_capacity()` for interior/one-past-end pointers; unknown capacity is now explicit (warning by default, refusal in strict mode) | `scratch/demo` exits 70 with `ESNOSPC 406` instead of SIGSEGV; `tests/crt/unit/test_safe_c_api.c` passes under UBSan; all prior suites remain green |

See [`ORDINARY_C_SAFETY.md`](ORDINARY_C_SAFETY.md) for activation, guarantees
and limits.

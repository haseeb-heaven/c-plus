# Per-method memory-safety matrix: C+ checked APIs vs safe Rust

**Authoritative consolidated run:** 2026-11-23T05:00:59Z (re-executed by the
parent session). All nine method groups landed; **no group is PENDING**.

**Toolchains:** `rustc 1.100.0` (`0ef1bc386 2026-11-09`), Apple clang 16
(`Apple clang version 16.0.0 (clang-1600.0.26.6)`), host macOS arm64 /
Darwin 27.2.0. Source of truth for every claim below is each group's
`NOTES.md`, its `compare.sh`, and the captured token streams in its generated
`out/`, `results/` or `.build/` directory.

---

> ## ⚠️ READ THIS FIRST — matching outcome TOKENS do NOT prove equivalent memory-safety guarantees
>
> - A matched token (for example `REFUSED / REFUSED`) records only that two
>   independently written programs **printed the same word** for the same
>   `(method, case)` label. It is **not** evidence of equal safety guarantees,
>   equal semantics, or correctness.
> - **C+ checked APIs enforce at run time** via `errno_t`/`errno` return codes
>   (e.g. `400` `ESNULLP`, `403` `ESLEMAX`, `404` `ESOVRLP`, `406` `ESNOSPC`,
>   `22` `CPLUS_SAFE_EINVAL`), `NULL` returns, bounded live-allocation /
>   recent-free caches, canary bytes, and (in `format`) a fail-fast trap that was
>   explicitly disabled so refusals stay observable. Dangerous code still
>   **compiles and runs**; it is refused only when executed. Many checked C+
>   entry points are called with `BOS_UNKNOWN`, which **disables the
>   object-size check**: an over-large `dmax` below the policy maximum would
>   still over-read. The metadata caches are finite — diagnosis/mitigation,
>   not a proof of lifetime safety.
> - **Rust uses compile-time ownership/borrow rules plus checked returns**:
>   no null references or overlapping `&mut` borrows are representable, a
>   second owner/double drop cannot be expressed, and the remaining cases use
>   `Option`/`None`, `Result`/`Err`, `checked_mul`, `try_reserve_exact`, or
>   panic-free bounds-checked `get`/`get_mut`. Several `REFUSED` rows are
>   deliberate **models** of situations safe Rust cannot construct.
> - **Sanitizer support is limited here.** The checked runtime was verified by
>   executed outcome tokens and `nm` symbol gates, not by a clean sanitizer
>   run. In this checkout the C+ safe-mode build is externally **BLOCKED** by
>   the `CPLUS_EXIT_ARITHMETIC` toolchain/header mismatch (see
>   `../SAFETY_MATRIX_NOTES.md`), so no sanitizer result is claimed. A missing
>   or blocked sanitizer configuration is never counted as a pass.
> - **NO Rust-equivalence claim. NO 100%-safety claim. NO general memory-safety
>   proof.** `BLOCKED` is neither a match nor a failure. These are a finite set
>   of deterministic outcome tokens on one host.

---

## Headline counts (verbatim from the consolidated run)

| metric | count |
|---|---:|
| cases run | **283** |
| matched tokens | **277** (sum of per-group matched counts; see reconciliation note below) |
| mismatched | **0** |
| blocked | **13** |

> **Reconciliation note (reported, not corrected):** the per-group `matched`
> integers printed by the consolidated run (20, 42, 42, 21, 29, 42, 42, 18, 21)
> sum to **277**, which is the correct arithmetic total. An earlier headline of **262** was a transcription error in the orchestrator's instructions to this document's author and is corrected here; no underlying result changed (0 mismatches in every group).
> Both figures are reproduced verbatim above and below; this document does not
> recompute, silently adjust, or fabricate a reconciliation. The headline
> follows the consolidated summary line. Every group independently reports
> `mismatched = 0` and `compare.sh` exited 0.

## Exact reproduction commands (from the repository root)

```sh
sh benchmarks/safety_compare/per_method/memory/compare.sh      # METHOD_MEMORY 20 20 0
sh benchmarks/safety_compare/per_method/strcopy/compare.sh     # METHOD_STRCOPY 42 42 0
sh benchmarks/safety_compare/per_method/strquery/compare.sh    # METHOD_STRQUERY 42 42 0 1
sh benchmarks/safety_compare/per_method/alloc/compare.sh       # METHOD_ALLOC 27 21 0 6
sh benchmarks/safety_compare/per_method/search/compare.sh      # METHOD_SEARCH 29 29 0 1
sh benchmarks/safety_compare/per_method/transform/compare.sh   # METHOD_TRANSFORM 42 42 0 0
sh benchmarks/safety_compare/per_method/field/compare.sh       # METHOD_FIELD 42 42 0 4
sh benchmarks/safety_compare/per_method/io/compare.sh          # METHOD_IO 18 18 0 1
sh benchmarks/safety_compare/per_method/format/compare.sh      # METHOD_FORMAT 21 21 0 0
```

Each driver re-probes symbol availability with `nm` at runtime, builds both
harnesses, runs each under `timeout 30`, diffs the token streams by
`(method, case)`, and nonzero-exits only on a real mismatch or a harness
build/run failure. Negative controls (deliberately perturbing one token) were
performed independently in `memory`, `strquery`, `field`, `io`, `search` and
`format` and each produced `mismatched = 1` with `compare.sh` exit 1, then was
restored byte-for-byte.

---

## Per-method matrix

Column `blocked` is per-row: for a whole method absent from the archive it is
`1` and that row has `cases run = 0` (no tokens fabricated). For `alloc` it is
the number of individual **cases** not expressible in safe Rust (those cases
are included in `cases run`). All other rows have `blocked = 0`. Tokens listed
are the literal strings printed by both harnesses; where C+ emits numeric
codes they are annotated as observed in the group's NOTES.md.

| Group | C method | C+ checked symbol (nm-verified) | Cases run | Matched | Mismatched | Blocked | C+ refusal mechanism | Rust mechanism | Exact outcome tokens observed |
|:--|:--|:--|--:|--:|--:|--:|:--|:--|:--|
| memory | `memcpy_s` | `cplus_sc_memcpy_s` (thin wrapper `cplus_memcpy_s`) | 5 | 5 | 0 | 0 | safeclib runtime check of `dmax`/`slen`, null and overlap; `errno_t` 0/400/403/404/406; 0xA5 canary after `dmax` | `copy_from_slice`, `get`/`get_mut(..n)` → `None`, `Option::None` for null, `RefCell::try_borrow_mut` → `Err` (compile-time borrow checker forbids overlapping `&mut`) | `OK` (rc=0); `REFUSED` (rc=400 null, 406 too-small, 403 SIZE_MAX, 404 overlap) |
| memory | `memmove_s` | `cplus_sc_memmove_s` (wrapper `cplus_memmove_s`) | 5 | 5 | 0 | 0 | same checked API; overlap is legal → `EOK` | `copy_within` (overlap allowed), `get(..n)` → `None`, `Option::None` | `OK` (rc=0, incl. overlap); `REFUSED` (rc=400/406/403) |
| memory | `memset_s` | `cplus_sc_memset_s` (wrapper `cplus_memset_s`) | 5 | 5 | 0 | 0 | checked `dmax`/null/size, `errno_t` codes; overlap case not applicable | `fill`, `get_mut(..n)` → `None`, `Option::None` | `OK` (rc=0); `REFUSED` (rc=400/406/403); `N/A` (overlap) |
| memory | `memcmp_s` | `cplus_sc_memcmp_s` (wrapper `cplus_memcmp_s`) | 5 | 5 | 0 | 0 | checked `dmax`/null/size; overlapping shared slices allowed → `EOK` | slice equality `a[..]==b[..]`, `get(..n)` → `None`, `Option::None` | `OK` (rc=0, incl. overlap); `REFUSED` (rc=400/406/403) |
| strcopy | `strcpy_s` | `cplus_sc_strcpy_s` | 7 | 7 | 0 | 0 | safeclib runtime constraint check, `ESNULLP`=400 / `ESNOSPC`=406, no-op constraint handler, 0xA5 canary | no raw pointers; null = `Option::None`, bound via `get`, out-of-bounds writes unrepresentable | `OK`, `REF_NULL`, `REF_NOSPC`, `CANARY_INTACT`; `unterminated_bounded`=`REF_NOSPC`, `truncation_dmax`=`REF_NOSPC` |
| strcopy | `strncpy_s` | `cplus_sc_strncpy_s` | 7 | 7 | 0 | 0 | same; explicit `slen` bound honoured, truncated result NUL-terminated | safe `get`/`len` checks; bounded truncation via safe API | `OK`, `REF_NULL`, `REF_NOSPC`, `CANARY_INTACT`; `unterminated_bounded`=`OK`, `truncation_dmax`=`OK` |
| strcopy | `strcat_s` | `cplus_sc_strcat_s` | 7 | 7 | 0 | 0 | safeclib `ESNULLP`/`ESNOSPC`, canary beyond `dmax` | `Option::None` + bounds-checked append | `OK`, `REF_NULL`, `REF_NOSPC`, `CANARY_INTACT`; `unterminated_bounded`=`REF_NOSPC`, `truncation_dmax`=`REF_NOSPC` |
| strcopy | `strncat_s` | `cplus_sc_strncat_s` | 7 | 7 | 0 | 0 | same; bounded `n` honoured | `Option::None` + bounds-checked bounded append | `OK`, `REF_NULL`, `REF_NOSPC`, `CANARY_INTACT`; `unterminated_bounded`=`OK`, `truncation_dmax`=`OK` |
| strcopy | `stpcpy_s` | `cplus_v__stpcpy_s_chk` (BOS_UNKNOWN) | 7 | 7 | 0 | 0 | STP worker reports via `*errp` 400/406 and NULL return; canary | safe bounded copy returning end view; null = `Option::None` | `OK`, `REF_NULL`, `REF_NOSPC`, `CANARY_INTACT`; `unterminated_bounded`=`REF_NOSPC`, `truncation_dmax`=`REF_NOSPC` |
| strcopy | `stpncpy_s` | `cplus_v__stpncpy_s_chk` (BOS_UNKNOWN) | 7 | 7 | 0 | 0 | bounded STP worker, `*errp` 400/406, canary | safe bounded copy; null = `Option::None` | `OK`, `REF_NULL`, `REF_NOSPC`, `CANARY_INTACT`; `unterminated_bounded`=`OK`, `truncation_dmax`=`OK` |
| strquery | `strlen_s` | **none** (no `cplus_sc_*`/`cplus_v_*` symbol in archive) | 0 | 0 | 0 | 1 | no linkable checked symbol — nothing to call | no Rust counterpart compared | no tokens emitted (BLOCKED, see `nm_evidence.txt`) |
| strquery | `strnlen_s` | `cplus_sc_strnlen_s` | 6 | 6 | 0 | 0 | runtime-checked query, policy bound `RSIZE_MAX_STR=4096`, BOS_UNKNOWN disables object-size check, canary bytes | `#![forbid(unsafe_code)]`, `Option<&str>=None`, bounded `get`/`find`-style scan | `OK:3`, `REFUSED`, `NOTFOUND:5`, `CANARY_OK` |
| strquery | `strchr_s` | `cplus_v__strchr_s_chk` | 6 | 6 | 0 | 0 | same checked-query family, `dmax` policy bound + canary | safe `find` on bounded `get` view | `OK:2`, `REFUSED`, `NOTFOUND[:n]`, `CANARY_OK` |
| strquery | `strrchr_s` | `cplus_v__strrchr_s_chk` | 6 | 6 | 0 | 0 | same | safe `rfind` on bounded `get` view | `OK:2`, `REFUSED`, `NOTFOUND[:n]`, `CANARY_OK` |
| strquery | `strstr_s` | `cplus_v__strstr_s_chk` | 6 | 6 | 0 | 0 | same | safe `find` on bounded `get` view | `OK:1`, `REFUSED`, `NOTFOUND[:n]`, `CANARY_OK` |
| strquery | `strcasestr_s` | `cplus_v__strcasestr_s_chk` | 6 | 6 | 0 | 0 | same (ASCII lowercasing on both sides) | `to_lowercase` + `find` on bounded view | `OK:2`, `REFUSED`, `NOTFOUND[:n]`, `CANARY_OK` |
| strquery | `strspn_s` | `cplus_v__strspn_s_chk` | 6 | 6 | 0 | 0 | same | safe `char_indices`/`matches` on bounded view | `OK:4`, `REFUSED`, `NOTFOUND[:n]`, `CANARY_OK` |
| strquery | `strcspn_s` | `cplus_v__strcspn_s_chk` | 6 | 6 | 0 | 0 | same | safe iterator scan on bounded view | `OK:3`, `REFUSED`, `NOTFOUND:0`, `CANARY_OK` |
| alloc | `malloc_s` | `malloc_s` (safecrt) | 5 | 5 | 0 | 0 | size/overflow check → `NULL` + `errno` (`EINVAL`); injected failure via `cplus_safe_alloc_fail_after(0)`; 8-byte `0xDEADBEEFCAFEBABE` canary at offset 64 | `Vec::with_capacity`, `checked_mul`, `try_reserve_exact` → `Err` | `OK`, `REFUSED`, `DEFINED` (zero-size), `CANARY_OK` |
| alloc | `calloc_s` | `calloc_s` (safecrt) | 5 | 5 | 0 | 0 | overflow → `NULL` + `errno=EOVERFLOW`; failure injection; canary | `checked_mul` → `None`, `try_reserve_exact` → `Err` | `OK`, `REFUSED`, `DEFINED`, `CANARY_OK` |
| alloc | `realloc_s` | `realloc_s` (safecrt) | 6 | 5 | 0 | 1 | injected failure asserts original block still live at 32 bytes → `NULL`; canary | `try_reserve_exact` leaves contents untouched on `Err`; `invalid` case unrepresentable → `BLOCKED` | `OK`, `REFUSED`, `DEFINED`, `PRESERVED`, `CANARY_OK`; `invalid`=`BLOCKED` (Rust) |
| alloc | `free_checked_s` | `free_checked_s` (safecrt) | 5 | 4 | 0 | 1 | live-allocation table + bounded free cache → `CPLUS_SAFE_EINVAL`=22 with `errno=EINVAL`; canary | `Option::take()` single-drop (second = `None`); foreign-pointer `invalid` unrepresentable → `BLOCKED` | `OK`, `REFUSED` (double free), `DEFINED` (null), `CANARY_OK`; `invalid`=`BLOCKED` (Rust) |
| alloc | `aligned_alloc_s` | `aligned_alloc_s` (safecrt) | 6 | 2 | 0 | 4 | alignment/size/overflow checks → `NULL` + `EINVAL`; canary | `Vec`/`Box` cannot express a caller-chosen runtime alignment; four cases `BLOCKED`, only `overflow`/`inject` comparable via `try_reserve_exact` → `Err` | `REFUSED` (`overflow`, `inject`); `valid`, `zero`, `bad_align`, `canary` = `BLOCKED` (Rust) |
| search | `bsearch_s` | `cplus_sc_upstream_bsearch_s` | 11 | 11 | 0 | 0 | runtime `errno` (`ESNULLP`/`ESLEMAX`); no object-size check (BOS branches omitted) | `binary_search_by`; null/absent refs unrepresentable; `Option`/length guards; closure captures context lexically | `FOUND`, `NOTFOUND`, `REFUSED`, `EMPTY`, `CTX_OK`, `CTX_LOST`, `INTACT` |
| search | `memrchr_s` | `cplus_sc_upstream_memrchr_s` | 9 | 9 | 0 | 0 | `errno_t` + `*resultp==NULL` (`ESNULLP`, `ESZEROL`, `ESLEMAX`, `ESNOTFND`); `ch<=255` | `iter().rposition` + `Option`/bounds guards; policy limit via explicit check | `FOUND`, `NOTFOUND`, `IDX3`, `REFUSED`, `INTACT` |
| search | `memchr_s` | `cplus_v_memchr_s` (newly vendored) | 9 | 9 | 0 | 0 | same checked family as `memrchr_s` | `iter().position` + `Option`/bounds guards | `FOUND`, `NOTFOUND`, `IDX0`, `REFUSED`, `INTACT` |
| search | `qsort_s` | **none** (macro only; no `_qsort_s_chk` linkable) | 0 | 0 | 0 | 1 | no linkable checked symbol | no comparison attempted | no tokens emitted (BLOCKED) |
| transform | `strtolowercase_s` | `cplus_v__strtolowercase_s_chk` | 6 | 6 | 0 | 0 | vendored `_chk` entry, BOS_UNKNOWN, `errno_t`/counts + silent constraint handler; canary | owned `String`, `get_mut`/`truncate`, explicit bounds, `to_lowercase` (ASCII inputs used) | `OK`, `REFUSED`, `TRUNC`, `CANARY_INTACT` |
| transform | `strtouppercase_s` | `cplus_v__strtouppercase_s_chk` | 6 | 6 | 0 | 0 | same | owned `String`, `get_mut`/`truncate`, `to_uppercase` | `OK`, `REFUSED`, `TRUNC`, `CANARY_INTACT` |
| transform | `strlcat` | `cplus_v_strlcat` | 6 | 6 | 0 | 0 | raw BSD API, no object-size check; caller-side precondition `dsize<=capacity` enforced before call; canary | same precondition check on safe slice; bounds-checked concat | `OK`, `REFUSED`, `TRUNC`, `CANARY_INTACT` |
| transform | `strlcpy` | `cplus_v_strlcpy` | 6 | 6 | 0 | 0 | same as `strlcat` | same precondition check on safe slice | `OK`, `REFUSED`, `TRUNC`, `CANARY_INTACT` |
| transform | `strnset_s` | `cplus_v__strnset_s_chk` | 6 | 6 | 0 | 0 | checked `_chk` entry, BOS_UNKNOWN, `errno_t` + handler, canary | `get_mut`/bounds check on owned buffer | `OK`, `REFUSED`, `TRUNC`, `CANARY_INTACT` |
| transform | `strset_s` | `cplus_v__strset_s_chk` | 6 | 6 | 0 | 0 | same | `get_mut`/bounds check on owned buffer | `OK`, `REFUSED`, `TRUNC`, `CANARY_INTACT` |
| transform | `strnterminate_s` | `cplus_v__strnterminate_s_chk` | 6 | 6 | 0 | 0 | checked `_chk` entry, forced NUL termination, canary | bounds-checked `truncate`/terminate on owned `String` | `OK`, `REFUSED`, `TERMINATED`, `CANARY_INTACT` |
| field | `strcmpfld_s` | `cplus_v__strcmpfld_s_chk` | 6 | 6 | 0 | 0 | upstream `_chk` runtime-constraint behaviour, BOS_UNKNOWN, guard bytes; `setlocale("C")` | **no C calls**; safe iterator `cmp` on `get` slices, `Option::None` for null | `CMP_LT`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strcpyfld_s` | `cplus_v__strcpyfld_s_chk` | 6 | 6 | 0 | 0 | same (`CHK_DEST_OVR`/`ESNOSPC` on one-over) | safe bounded copy, `Option::None`, bounds check | `COPY_OK`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strcoll_s` | `cplus_v__strcoll_s_chk` | 6 | 6 | 0 | 0 | same, locale pinned to `C` | `str::cmp` on NUL-prefix (ASCII only) | `CMP_LT`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strfirstchar_s` | `cplus_v__strfirstchar_s_chk` | 6 | 6 | 0 | 0 | same | safe `position` on bounded slice | `POS_2`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strlastchar_s` | `cplus_v__strlastchar_s_chk` | 6 | 6 | 0 | 0 | same | safe `rposition` on bounded slice | `POS_5`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strfirstdiff_s` | `cplus_v__strfirstdiff_s_chk` | 6 | 6 | 0 | 0 | same | safe iterator `position` on zipped bounded slices | `DIFF_2`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strlastdiff_s` | `cplus_v__strlastdiff_s_chk` | 6 | 6 | 0 | 0 | same | safe iterator `rposition` on zipped bounded slices | `DIFF_4`, `REFUSED`, `DELIM_OK`, `CANARY_INTACT` |
| field | `strisalphanumeric_s` | **none** (absent from archive) | 0 | 0 | 0 | 1 | no linkable checked symbol | compiled out of both harnesses | no tokens emitted (BLOCKED) |
| field | `strisascii_s` | **none** (absent from archive) | 0 | 0 | 0 | 1 | no linkable checked symbol | compiled out of both harnesses | no tokens emitted (BLOCKED) |
| field | `strisdigit_s` | **none** (absent from archive) | 0 | 0 | 0 | 1 | no linkable checked symbol | compiled out of both harnesses | no tokens emitted (BLOCKED) |
| field | `strishex_s` | **none** (absent from archive) | 0 | 0 | 0 | 1 | no linkable checked symbol | compiled out of both harnesses | no tokens emitted (BLOCKED) |
| io | `fopen_s` | `cplus_fopen_s` (safecrt) | 3 | 3 | 0 | 0 | NULL name/mode → `NULL` + `EINVAL` | `File::open`/`OpenOptions`; null = `Option::None`, no mode string | `REFUSED` (`null_path`, `null_mode`), `OK` (`valid_open`) |
| io | `fread_s` | `cplus_fread_s` (safecrt) | 6 | 6 | 0 | 0 | null buf/stream or zero size/count → `0`; clamps `count=min(count,bmax/size)`; 0x5A canaries | `read_exact` into bounds-checked `&mut [u8]`; oversized slice refused before I/O | `OK`, `REFUSED`, `OK` (`canary_intact`) |
| io | `fwrite_s` | `cplus_fwrite_s` (safecrt) | 6 | 6 | 0 | 0 | identical guard/clamp behaviour to `fread_s`; canaries | `write_all` into bounds-checked slice | `OK`, `REFUSED`, `OK` (`canary_intact`) |
| io | `fclose_s` | `cplus_fclose_s` (safecrt) | 3 | 3 | 0 | 0 | bounded recent-close cache → `EOF` on null/closed/double close (UAF guard) | `Drop` via `Option::take()`; aliasing unrepresentable, second `take()` = `None` | `OK`, `REFUSED` (`double_close`, `null_stream`) |
| io | `tmpfile_s` | **none** (only unchecked `cplus_tmpfile` exists) | 0 | 0 | 0 | 1 | no checked symbol | no direct safe-Rust equivalent | no tokens emitted (BLOCKED) |
| format | `sprintf_s` | `cplus_sprintf_s_checked` | 5 | 5 | 0 | 0 | trap disabled (`cplus_runtime_set_violation_trap(0)`) so guarded violations return `-1`; bounded-vsnprintf semantics → `TRUNC`; `cplus_fmt_has_percent_n` refuses `%n`; stderr diagnostics; canary | `write!` into fixed-capacity writer → `fmt::Error`; `Option<&mut [u8]>=None`; no `%n` grammar | `OK`, `REFUSED`, `TRUNC` |
| format | `snprintf_s` | `cplus_snprintf_s_checked` | 5 | 5 | 0 | 0 | same | same | `OK`, `REFUSED`, `TRUNC` |
| format | `vsnprintf_s` | `cplus_vsnprintf_s` | 5 | 5 | 0 | 0 | same | same | `OK`, `REFUSED`, `TRUNC` |
| format | `strtol` | `cplus_strtol` (platform-backed wrapper) | 2 | 2 | 0 | 0 | standard overflow contract `errno == ERANGE` | `parse::<i64>()` → `Err(PosOverflow)` | `OK`, `OVERFLOW_REFUSED` |
| format | `strtoul` | `cplus_strtoul` (platform-backed wrapper) | 2 | 2 | 0 | 0 | `errno == ERANGE` | `parse::<u64>()` → `Err(PosOverflow)` | `OK`, `OVERFLOW_REFUSED` |
| format | `strtoimax` | `cplus_strtoimax` (platform-backed wrapper) | 2 | 2 | 0 | 0 | `errno == ERANGE` (LP64 64-bit) | `parse::<i64>()` → `Err(PosOverflow)` | `OK`, `OVERFLOW_REFUSED` |

---

## Group totals (verbatim summary lines from the consolidated run)

The `blocked` column follows each driver's own convention. For `strquery`,
`search`, `field` and `io` it counts **methods with no linkable checked
symbol**, which are excluded from `cases`. For `alloc` it counts **individual
cases** not expressible in safe Rust, which are included in `cases`. `memory`,
`strcopy`, `transform` and `format` print no blocked field (= 0).

| Group | Summary line | Cases | Matched | Mismatched | Blocked | Blocked means |
|:--|:--|--:|--:|--:|--:|:--|
| memory | `METHOD_MEMORY 20 20 0` | 20 | 20 | 0 | 0 | — |
| strcopy | `METHOD_STRCOPY 42 42 0` | 42 | 42 | 0 | 0 | — |
| strquery | `METHOD_STRQUERY 42 42 0 1` | 42 | 42 | 0 | 1 | `strlen_s` (no symbol) |
| alloc | `METHOD_ALLOC 27 21 0 6` | 27 | 21 | 0 | 6 | 6 unrepresentable cases |
| search | `METHOD_SEARCH 29 29 0 1` | 29 | 29 | 0 | 1 | `qsort_s` (macro only, no symbol) |
| transform | `METHOD_TRANSFORM 42 42 0 0` | 42 | 42 | 0 | 0 | — |
| field | `METHOD_FIELD 42 42 0 4` | 42 | 42 | 0 | 4 | four `stris*` methods absent |
| io | `METHOD_IO 18 18 0 1` | 18 | 18 | 0 | 1 | `tmpfile_s` (no checked symbol) |
| format | `METHOD_FORMAT 21 21 0 0` | 21 | 21 | 0 | 0 | — |

## Blocked methods — explicit inventory

No tokens were fabricated for any of these. Each is re-derived at runtime by
`nm` on the archives, so a later rebuild that adds a symbol activates the
guard-compiled test paths automatically.

| Group | Blocked method(s) | Evidence in NOTES.md |
|:--|:--|:--|
| strquery | `strlen_s` | no `cplus_sc_*`/`cplus_v_*` symbol; `strnlen_s` is the bounded API |
| search | `qsort_s` | safeclib header macro expands to `_qsort_s_chk`, which has no definition in any archive |
| field | `strisalphanumeric_s`, `strisascii_s`, `strisdigit_s`, `strishex_s` | four `nm` greps empty, exit 1; no classification TU in `libc/src/safecrt_vendor/` |
| io | `tmpfile_s` | only unchecked `cplus_tmpfile` in hardened archive; no checked entry |
| alloc | 6 cases: `realloc_s invalid`, `free_checked_s invalid`, `aligned_alloc_s valid/zero/bad_align/canary` | scenario unrepresentable with safe APIs (invalid/foreign pointers; caller-chosen runtime alignment), not a Rust check failure |

## Non-claims

- **No Rust equivalence.** Identical tokens are equality of printed words on a
  fixed set of deterministic cases, not equality of guarantees. C+ runtime
  error codes and caches are not the same mechanism as Rust compile-time
  ownership/borrow rules plus checked returns.
- **No Rust-equivalence or 100%-safety claim.** Sanitizer support is limited
  here: the C+ safe-mode build is externally BLOCKED by the
  `CPLUS_EXIT_ARITHMETIC` header/runtime mismatch, and no clean sanitizer run
  is asserted. A clean sanitizer execution would be evidence about one
  execution only.
- **No proof of general memory safety.** `BOS_UNKNOWN` calls disable the
  object-size check; canaries cover only the tested regions; several checked
  functions have no object-size validation at all; locale/Unicode corpora are
  restricted to ASCII where the APIs differ.
- **Blocked is not a pass and not a zero.** 13 blocked items are accounted for
  separately from the 277 matched-token figure.
- This is one host, one toolchain pair (`rustc 1.100.0`, Apple clang 16), and a
  finite per-method case set. It is not a speed result and not a live-game
  claim.

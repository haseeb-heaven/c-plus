# Definitive fuzz catalog and 100-configuration plan (F001–F100)

Branch: `agent/modern-stdlib`.
Refreshed 2026-11-23 against the current corpus. The original F001–F100 plan in
§3 was written before the `f100_*` harnesses existed and uses family-based IDs;
the parallel agents that wrote the harnesses used their own `F-001..F-100`
namespace. **The two namespaces do not match one-for-one.** The authoritative
as-built registry (exact paths) is §1.4; status updates in §3 cite those paths,
never an ID coincidence. Nothing here claims completion — see §4 and the
completion ledger for the still-unmet criteria.

## 0. Reading rules and the headline caveat

- **100 configurations ≠ 100% safety proof.** A configuration is a *defined,
  reproducible input-generator + API-target + oracle*. It does not prove the
  absence of defects in the covered APIs, let alone the library. Coverage is
  bounded by the generated grammar, the seed(s), the case count, the build
  modes, and the strength of the oracle (many existing targets only *call* an
  API and check "no crash"; that is weaker than an invariant/shadow-model
  check). No percentage in this document is presented without its denominator.
- **Never generate raw UB and call it a bug.** A crash is a finding only when
  every argument passed satisfies the documented preconditions, **or** the API
  is a *checked* API whose contract explicitly defines a recoverable error for
  that argument. Deliberately fatal conditions belong in a forked child under
  an external timeout, never in the main fuzz process (see
  `docs/stdlib/HP_FUZZ_NOTES.md` §6).
- **EXISTING** means: a named harness in the three scoped directories actually
  drives the stated API surface today, with a cited file path. It does **not**
  mean the configuration is strong, well-oracled, or wired into a mutation
  campaign. Where the only driver is a single fixed input, that is called out.
- **TO_ADD** means: no scoped harness covers it; a new configuration is needed.

## 1. Inventory method and exact counts

> §1.1–§1.3 describe the **pre-f100 baseline** harnesses that existed when this
> catalog was first written. They are retained as historical context. The
> current as-built 100-configuration corpus (110 `f100_*` files) is inventoried
> in **§1.4**, which is the denominator that applies to the refreshed statuses
> in §3–§6. Do not add the two inventories together.

Scoped directories searched, exhaustively:

1. `tests/crt/fuzz/`
2. `tests/libc/`
3. `tests/stdlib/fuzz/`

Method: enumerate every `*.c`/`*.cp` file, read each entry point
(`LLVMFuzzerTestOneInput` vs standalone `main`), record the seed constant, the
fixed case count, the APIs touched, the language (C vs C+), the build/sanitizer
mode used by its runner, and whether a runner script actually executes it.

### 1.1 Exact denominators

| Bucket | Count | Denominator detail |
| --- | --- | --- |
| Fuzz-specific source files in the three scoped dirs | 25 of 25 | 18 in `tests/crt/fuzz/` + 2 in `tests/libc/` + 5 in `tests/stdlib/fuzz/` |
| Real fuzz harnesses (entry point or standalone driver) | **23 of 25** | 18 libFuzzer entry-point targets + 5 standalone deterministic drivers |
| Pure helper `main` files (not harnesses) | 2 of 25 | `tests/crt/fuzz/fuzz_smoke.c`, `tests/stdlib/fuzz/fuzz_main.c` |
| libFuzzer entry-point harnesses | 18 of 23 | 15 C CRT (`tests/crt/fuzz/`) + 3 C+ stdlib (`.cp`) |
| Standalone deterministic drivers | 5 of 23 | 2 CRT + 2 libc + 1 C+ (`fuzz_hp.cp`) |
| Harnesses executed by at least one runner | **21 of 23** | see §3 |
| Harnesses with no automated runner | 2 of 23 | `fuzz_all_exported.c`, `fuzz_hp.cp` (manual-only) |

Additional notes on the denominator:

- `tests/libc/` also contains 99 `test_*.c` unit/attack/corpus/scenario files.
  They are **not** fuzz harnesses and are excluded from the 23. They are the
  home of deliberately invalid-pointer / `SIZE_MAX` / abort-path tests; this
  catalog does not reclassify them as fuzzing.
- `tests/crt/adversarial/test_raii50_fuzz_memcpy_family.c` is a standalone
  deterministic fuzz-style driver (LCG seed `0x4E3D0F1B`, 2500+3000+3000+3000+
  3000 inner draws) but lives **outside** the three scoped directories. It is
  excluded from the 23 and cited in §4 only as adjacent coverage.
- `build/fuzz/probe.c` is a generated libFuzzer-availability probe, not a
  harness.

### 1.2 Per-harness inventory

Legend: **LF** = defines `LLVMFuzzerTestOneInput`; **STD** = standalone with own
`main`; **helper** = `main` that feeds one input to a target. "Runner" column
names a script/CI step that actually executes it; "manual" means no automation.

| # | Harness (path) | Type / lang | Seed strategy | Fixed case count | APIs touched | Mode(s) | Runner |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `tests/crt/fuzz/fuzz_memcpy_s.c` | LF / C99 | libFuzzer input bytes; CI feeds one fixed `{1,2,3,4}` seed via `fuzz_smoke.c` | 1 fixed input in CI | `cplus_memcpy_s` (dmax/smax/n variants) | C99, plain `-O1` | `tests/crt/run_all_crt.sh`; CI "CRT all checks" |
| 2 | `tests/crt/fuzz/fuzz_strcpy_s.c` | LF / C99 | same single fixed `"abc"` seed | 1 fixed input in CI | `cplus_strcpy_s`, `cplus_strncpy_s` | C99, plain | `tests/crt/run_all_crt.sh` |
| 3 | `tests/crt/fuzz/fuzz_alloc_s.c` | LF / C99 | same single fixed seed | 1 fixed input in CI | `malloc_s`, `realloc_s`, `free_s`, `calloc_s` | C99, plain | `tests/crt/run_all_crt.sh` |
| 4 | `tests/crt/fuzz/fuzz_format_s.c` | LF / C99 | same single fixed seed | 1 fixed input in CI | `cplus_snprintf_s` | C99, plain | `tests/crt/run_all_crt.sh` |
| 5 | `tests/crt/fuzz/fuzz_memory.c` | LF / C99 | input bytes; UBSan smoke main uses 16 bytes `0..15` | 1 fixed input | `cplus_memcpy_s`, `cplus_memmove_s`, `cplus_memset_s`, `cplus_memcmp_s`, `cplus_memzero_s` | C99, `-fsanitize=undefined -fno-sanitize-recover=all` | `tests/crt/fuzz/run_fuzz_smoke.sh`; CI "Fuzz smoke (UBSan)" |
| 6 | `tests/crt/fuzz/fuzz_strings.c` | LF / C99 | input bytes; smoke seed `"abc"` | 1 fixed input | `cplus_strcpy_s`, `cplus_strncpy_s`, `cplus_strcat_s`, `cplus_strncat_s`, `cplus_strcmp_s`, `cplus_strnlen_s` | C99, UBSan | same as #5 |
| 7 | `tests/crt/fuzz/fuzz_allocator.c` | LF / C99 | input bytes; smoke seed `{1,2,3,4}` | 1 fixed input | `malloc_s`, `realloc_s`, `free_s`, `calloc_s` | C99, UBSan | same as #5 |
| 8 | `tests/crt/fuzz/fuzz_format.c` | LF / C99 | input bytes with all `%` neutralized to `_`; smoke seed `"x"` | 1 fixed input | `cplus_snprintf_s`, `cplus_sprintf_s` | C99, UBSan | same as #5 |
| 9 | `tests/crt/fuzz/fuzz_stdio.c` | LF / C99 | input bytes written to fixed `/tmp/cplus_fuzz_stdio.tmp`; smoke seed `{1,2,3,4}` | 1 fixed input | `cplus_fopen_s`, `cplus_fwrite_s`, `cplus_fread_s`, `cplus_fclose_s` | C99, UBSan | same as #5 |
| 10 | `tests/crt/fuzz/fuzz_realloc.c` | LF / C99 | input bytes; every 7th byte forces `SIZE_MAX`; smoke seed 16 bytes | 1 fixed input | `realloc_s`, `free_s` (+ log level off) | C99, UBSan | same as #5 |
| 11 | `tests/crt/fuzz/fuzz_free.c` | LF / C99 | input byte `% 4` selects double-free / non-owned / NULL / interior pointer; smoke seed `0..7` | 1 fixed input | `free_checked_s`, `malloc_s`, `free_s` | C99, UBSan | same as #5 |
| 12 | `tests/crt/fuzz/fuzz_parsing.c` | LF / C99 | input bytes → NUL-terminated buffer; smoke seed `"one,two;three:four"` | 1 fixed input | `cplus_strtok_s` | C99, UBSan | same as #5 |
| 13 | `tests/crt/fuzz/fuzz_error.c` | LF / C99 | input bytes drive NULL-vs-valid dest and constraint mode `0..4`; smoke seed 8 bytes | 1 fixed input | `cplus_memcpy_s`, `cplus_safe_set_constraint_mode`, `cplus_safe_stats_dump` | C99, UBSan | same as #5 |
| 14 | `tests/crt/fuzz/fuzz_lifecycle.c` | LF / C99 | input bytes; smoke seed `"abcdef"` | 1 fixed input | `cplus_fopen_s`, `cplus_fwrite_s`, `cplus_fread_s`, `cplus_fclose_s` (double close) | C99, UBSan | same as #5 |
| 15 | `tests/crt/fuzz/fuzz_ownership.c` | LF / C99 | input bytes drive transfer/free decisions; smoke seed `{10,20,31,40,51,60}` | 1 fixed input | `cplus_safe_transfer_ownership`, `cplus_safe_ptr_is_live`, `malloc_s`, `free_s` | C99, UBSan | same as #5 |
| 16 | `tests/crt/fuzz/fuzz_all_exported.c` | STD / C99 | deterministic LCG seed `0xC0FFEE` | 1500 iterations | ~50 safe + unsafe memory/string/alloc/stdio/format/platform/log APIs | C99, `-O2` in CI "Combined fuzz harness"; also ASan+UBSan job uses `fuzz_all_safe_apis.c` not this one | **manual only** (no runner references it) |
| 17 | `tests/crt/fuzz/fuzz_all_safe_apis.c` | STD / C99 | deterministic LCG seed `1` | 2000 iterations | `memcpy_s`/`memmove_s`/`memset_s`/`memcmp_s`/`memzero_s`, `strcpy_s`/`strncpy_s`/`strcat_s`/`strncat_s`/`strcmp_s`/`strnlen_s`, alloc_s family, `fopen_s`/`fwrite_s`/`fread_s`/`fprintf_s`/`fclose_s`, `snprintf_s`/`sprintf_s`/`vsnprintf_s` | C99 `-O2` plain **and** `-fsanitize=address,undefined` (CI) | CI "Combined fuzz harness" + "ASan+UBSan smoke" |
| 18 | `tests/libc/fuzz_libc.c` | STD / C99 | LCG seed `0x5EED1234`, state reset each pass | 300 iterations × 8 APIs × 2 logging passes = **4800 cases** | `cplus_memcpy`, `cplus_memmove` (incl. overlap), `cplus_memset`, `cplus_memchr`, `cplus_memrchr`, `cplus_strnlen`, `cplus_strncpy`, `cplus_strncat`; log on/off digest parity | C99 plain; UBSan command documented | `tests/libc/run_fuzz.sh` (CI), `tests/libc/run_fuzz_libc.sh` (evidence) |
| 19 | `tests/libc/fuzz_libc2.c` | STD / C99 | LCG seed `0xC0FFEE` | 5000 iterations | `strndup`, `strtok_r`, `strsep`, `memmem`, `strcasestr`, `strchrnul`, `strverscmp`, `div`, `ldiv`, `llabs`, `atoll`, `wcslen`, `wcscpy`, `wcscmp`, `isascii`, `toascii` | C99 plain | `tests/libc/run_fuzz.sh` (CI) |
| 20 | `tests/stdlib/fuzz/fuzz_json.cp` | LF / C+ | libFuzzer corpus `json-1` (`{"a":1}`), `json-2` (`[]`) + mutation; fallback driver seed `0x9e3779b97f4a7c15` xorshift | 200 (`run_all.sh`) / 500 (default) runs | `json` parse, `json_view.size/at`, `get_int` | C+; libFuzzer+ASan if available, else mutate driver with UBSan/bounds/null/alignment | `tests/stdlib/fuzz/run_fuzz.sh`, `tests/stdlib/run_all.sh` |
| 21 | `tests/stdlib/fuzz/fuzz_string.cp` | LF / C+ | libFuzzer corpus `string-1` (`a,b,c`) + mutation; same fallback xorshift | 200 / 500 runs | `string.append/reserve/substr/field`, `vector.push_back/pop_back/at_checked`, `string_view` | same as #20 | same as #20 |
| 22 | `tests/stdlib/fuzz/fuzz_containers.cp` | LF / C+ | libFuzzer mutation (no dedicated corpus file) + fallback xorshift | 200 / 500 runs | `vector`, `intset`, `deque`, `list`, `stack`, `queue`, `intmap` | same as #20 | same as #20 |
| 23 | `tests/stdlib/fuzz/fuzz_hp.cp` | STD / C+ | 64-bit LCG seed `0xC0FFEE` (high 32 bits) | `CASES = 1200` valid ops, `NOPS = 18` | `error`, `string`/`string_view`, `vector`/`slice`, `intmap`/`intset`/`strmap`/`deque`/`list`/`stack`/`queue`, `io.hp` EOF readers | C+ default; `--strict --sanitize=none`; `--strict --sanitize=undefined` | **manual** per `docs/stdlib/HP_FUZZ_NOTES.md` §5 |

Helpers (excluded from the 23): `tests/crt/fuzz/fuzz_smoke.c` (calls one target
with a fixed 4-byte buffer), `tests/stdlib/fuzz/fuzz_main.c` (feeds all corpus
files plus N xorshift-random buffers to one target; N = 200 via `run_all.sh`,
500 by default).

### 1.3 Runner / mode matrix (denominators stated)

| Runner | Harnesses it executes | Count |
| --- | --- | --- |
| `tests/crt/run_all_crt.sh` ("Safe CRT fuzz smoke") | #1–#4, one fixed input each | 4 of 23 |
| `tests/crt/fuzz/run_fuzz_smoke.sh` (UBSan) | #5–#15, one fixed input each | 11 of 23 |
| `.github/workflows/libc-crt.yml` "Combined fuzz harness" + ASan job | #17 (twice: plain and ASan+UBSan) | 1 of 23 |
| `tests/libc/run_fuzz.sh` (CI "Libc fuzz") | #18, #19 | 2 of 23 |
| `tests/libc/run_fuzz_libc.sh` (evidence aggregator) | #18 only | 1 of 23 |
| `tests/stdlib/fuzz/run_fuzz.sh` (via `run_all.sh 200`) | #20, #21, #22 | 3 of 23 |
| no automated runner (manual commands in docs) | #16, #23 | 2 of 23 |

Sum of distinct executed harnesses: 4 + 11 + 1 + 2 + 3 = **21 of 23**.

Sanitizer reality check: ASan is documented as unavailable on this host
(`docs/libc/LIBC_FUZZ_NOTES.md` §AddressSanitizer status,
`docs/stdlib/HP_FUZZ_NOTES.md` §7); the CI ASan+UBSan job runs only #17 on Linux.
So UBSan/plain are the effective default modes for the other 22 of 23 harnesses.

### 1.4 As-built 100-configuration corpus (current source of truth for statuses)

The 100 configurations now exist as `f100_*` files under exactly two scoped
directories: `tests/crt/fuzz/` (60 `.c` harnesses) and `tests/stdlib/fuzz/`
(40 `.cp` harnesses). A read-only inventory of the current tree finds **110**
`f100_*` regular files: 100 config sources + 10 support files (3 `.h`, 1 `.hp`,
2 `.json`, 4 `.sh`). The support files are not harnesses; the runner classifies
them `SKIPPED / unsupported harness extension`.

**Namespace rule.** The original plan IDs in §3 (F001 = unchecked `memcpy`,
F021 = case-insensitive string family, F050 = `calloc_s`, …) are a *planning*
namespace assigned before the harnesses were written. The as-built IDs below
(F-001 = `cplus_memcpy`, F-021 = `malloc_s`, F-050 = format `%n` refusal, …)
come from the owning agents' notes files. A shared number does **not** mean a
shared API. §3 status edits below therefore reconcile by *API intent*, with the
exact source path cited, and a `Reconciled in §1.4` note. Where the plan's intent
has no as-built harness, the status stays **TO_ADD**.

Runner evidence (all denominators stated, no percentages):

- Current runner result supplied by the parent session: `discovered = 101`,
  `PASS = 95`, `SKIPPED = 6`, `FAIL = 0`, `BUILD_FAILURE = 0`, `TIMEOUT = 0`,
  `BLOCKED = 0`, after the `cplus_aligned_alloc` side-table fix (as-built F-034).
  `95 + 6 = 101`, so every discovered file was `PASS` or `SKIPPED`.
- Last machine-readable snapshot on disk: `build/fuzz100/summary.json`, mtime
  `2026-11-23T01:25:27Z`, records `discovered = 104`, `executed = 98`,
  `PASS = 98`, `SKIPPED = 6`, `FAIL = 0`, `BUILD_FAILURE = 0`. It predates six
  thread/JSON files (`f100_json_cycles.cp`, `f100_json_drop.cp`,
  `f100_json_configs.json`, `f100_json_run.sh`, `f100_thread_configs.json`,
  `f100_thread_run.sh`), so it is point-in-time, not final. This agent did not
  re-run the runner: that would write files outside its strict ownership.
- `SKIPPED = 6` in the last snapshot = `f100_alloc_harness.h`,
  `f100_alloc_life_run.sh`, `f100_fmt_common.h`, `f100_fmt_run.sh`,
  `f100_io_common.h`, `f100_cont_engine.hp`. The four later support files
  (`.json`/`.sh`) are also SKIPPED by extension when discovered.
- **No recorded run has 100/100 config `PASS`.** Both recorded snapshots are
  below 100 `PASS`, and the runner counts a case as `PASS` only on build exit 0
  and process exit 0 with no expected-output fixture (per `tests/fuzz100/README.md`
  §"Exact classification"). A file path or an agent's own notes is not a runner
  result. C4 of the completion ledger stays **NOT MET**.
- Some new sources are not yet wired into the wider build scripts; that is
  being handled by a different agent and is not evidence of closure here.

As-built registry (ID / exact path / runner evidence in the last snapshot):

**A CRT memory/string (F-001..F-020)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-001 | `tests/crt/fuzz/f100_mem_memcpy_bounded.c` | PASS |
| F-002 | `tests/crt/fuzz/f100_mem_memmove_bounded.c` | PASS |
| F-003 | `tests/crt/fuzz/f100_mem_memmove_overlap.c` | PASS |
| F-004 | `tests/crt/fuzz/f100_mem_memset_bounded.c` | PASS |
| F-005 | `tests/crt/fuzz/f100_mem_memcmp_bounded.c` | PASS |
| F-006 | `tests/crt/fuzz/f100_mem_memcpy_s_dmax.c` | PASS |
| F-007 | `tests/crt/fuzz/f100_mem_memmove_s.c` | PASS |
| F-008 | `tests/crt/fuzz/f100_mem_memset_s.c` | PASS |
| F-009 | `tests/crt/fuzz/f100_mem_memcmp_s.c` | PASS |
| F-010 | `tests/crt/fuzz/f100_mem_memzero_s.c` | PASS |
| F-011 | `tests/crt/fuzz/f100_str_strlen_bounded.c` | PASS |
| F-012 | `tests/crt/fuzz/f100_str_strnlen_bounded.c` | PASS |
| F-013 | `tests/crt/fuzz/f100_str_strcpy_bounded.c` | PASS |
| F-014 | `tests/crt/fuzz/f100_str_strncpy_s.c` | PASS |
| F-015 | `tests/crt/fuzz/f100_str_strcat_s.c` | PASS |
| F-016 | `tests/crt/fuzz/f100_str_strncat_s.c` | PASS |
| F-017 | `tests/crt/fuzz/f100_str_strcmp_s.c` | PASS |
| F-018 | `tests/crt/fuzz/f100_str_strnlen_s.c` | PASS |
| F-019 | `tests/crt/fuzz/f100_str_constraint_matrix.c` | PASS |
| F-020 | `tests/crt/fuzz/f100_str_log_parity.c` | PASS |

**B CRT alloc/lifecycle (F-021..F-040)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-021 | `tests/crt/fuzz/f100_alloc_f021_malloc.c` | PASS |
| F-022 | `tests/crt/fuzz/f100_alloc_f022_calloc.c` | PASS |
| F-023 | `tests/crt/fuzz/f100_alloc_f023_realloc.c` | PASS |
| F-024 | `tests/crt/fuzz/f100_alloc_f024_free.c` | PASS |
| F-025 | `tests/crt/fuzz/f100_alloc_f025_aligned.c` | PASS |
| F-026 | `tests/crt/fuzz/f100_alloc_f026_sidetable.c` | PASS |
| F-027 | `tests/crt/fuzz/f100_alloc_f027_transfer.c` | PASS |
| F-028 | `tests/crt/fuzz/f100_alloc_f028_injection.c` | PASS |
| F-029 | `tests/crt/fuzz/f100_alloc_f029_canary.c` | PASS |
| F-030 | `tests/crt/fuzz/f100_life_f030_cplus_malloc.c` | PASS |
| F-031 | `tests/crt/fuzz/f100_life_f031_cplus_calloc.c` | PASS |
| F-032 | `tests/crt/fuzz/f100_life_f032_cplus_realloc.c` | PASS |
| F-033 | `tests/crt/fuzz/f100_life_f033_cplus_free.c` | PASS |
| F-034 | `tests/crt/fuzz/f100_life_f034_cplus_aligned.c` | PASS (after side-table fix) |
| F-035 | `tests/crt/fuzz/f100_life_f035_cplus_meta.c` | PASS |
| F-036 | `tests/crt/fuzz/f100_life_f036_cplus_transfer.c` | PASS |
| F-037 | `tests/crt/fuzz/f100_life_f037_cplus_injection.c` | PASS |
| F-038 | `tests/crt/fuzz/f100_life_f038_repeated_drop.c` | PASS |
| F-039 | `tests/crt/fuzz/f100_life_f039_canary.c` | PASS |
| F-040 | `tests/crt/fuzz/f100_life_f040_custom_alloc_skip.c` | PASS (emits explicit SKIP for custom-allocator plug-in) |

**C CRT format/IO (F-041..F-060)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-041 | `tests/crt/fuzz/f100_fmt_snprintf_trunc.c` | PASS |
| F-042 | `tests/crt/fuzz/f100_fmt_snprintf_int.c` | PASS |
| F-043 | `tests/crt/fuzz/f100_fmt_snprintf_float.c` | PASS |
| F-044 | `tests/crt/fuzz/f100_fmt_vsnprintf.c` | PASS |
| F-045 | `tests/crt/fuzz/f100_fmt_sprintf.c` | PASS |
| F-046 | `tests/crt/fuzz/f100_fmt_printf.c` | PASS |
| F-047 | `tests/crt/fuzz/f100_fmt_fprintf.c` | PASS |
| F-048 | `tests/crt/fuzz/f100_fmt_checked.c` | PASS |
| F-049 | `tests/crt/fuzz/f100_fmt_sscanf.c` | PASS |
| F-050 | `tests/crt/fuzz/f100_fmt_contract.c` | PASS |
| F-051 | `tests/crt/fuzz/f100_io_open_close.c` | PASS |
| F-052 | `tests/crt/fuzz/f100_io_readwrite.c` | PASS |
| F-053 | `tests/crt/fuzz/f100_io_readline.c` | PASS |
| F-054 | `tests/crt/fuzz/f100_io_token.c` | PASS |
| F-055 | `tests/crt/fuzz/f100_io_strtol.c` | PASS |
| F-056 | `tests/crt/fuzz/f100_io_strtoll.c` | PASS |
| F-057 | `tests/crt/fuzz/f100_io_strtod.c` | PASS |
| F-058 | `tests/crt/fuzz/f100_io_atoi.c` | PASS |
| F-059 | `tests/crt/fuzz/f100_io_fscanf.c` | PASS |
| F-060 | `tests/crt/fuzz/f100_io_roundtrip.c` | PASS |

**D stdlib containers/vector (F-061..F-075)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-061 | `tests/stdlib/fuzz/f100_cont_f061_collide_int.cp` | PASS |
| F-062 | `tests/stdlib/fuzz/f100_cont_f062_tomb_intmap.cp` | PASS |
| F-063 | `tests/stdlib/fuzz/f100_cont_f063_tomb_intset.cp` | PASS |
| F-064 | `tests/stdlib/fuzz/f100_cont_f064_collide_strmap.cp` | PASS |
| F-065 | `tests/stdlib/fuzz/f100_cont_f065_tomb_strmap.cp` | PASS |
| F-066 | `tests/stdlib/fuzz/f100_cont_f066_deque_ring.cp` | PASS |
| F-067 | `tests/stdlib/fuzz/f100_cont_f067_list_churn.cp` | PASS |
| F-068 | `tests/stdlib/fuzz/f100_cont_f068_stack_growth.cp` | PASS |
| F-069 | `tests/stdlib/fuzz/f100_cont_f069_queue_ring.cp` | PASS |
| F-070 | `tests/stdlib/fuzz/f100_vec_f070_capacity.cp` | PASS |
| F-071 | `tests/stdlib/fuzz/f100_vec_f071_erase.cp` | PASS |
| F-072 | `tests/stdlib/fuzz/f100_vec_f072_stale.cp` | PASS |
| F-073 | `tests/stdlib/fuzz/f100_vec_f073_faults.cp` | PASS |
| F-074 | `tests/stdlib/fuzz/f100_cont_f074_faults.cp` | PASS |
| F-075 | `tests/stdlib/fuzz/f100_cont_f075_balance.cp` | PASS |

**E stdlib string/file/memory (F-076..F-085)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-076 | `tests/stdlib/fuzz/f100_strview_append.cp` | PASS |
| F-077 | `tests/stdlib/fuzz/f100_strview_reserve_copy.cp` | PASS |
| F-078 | `tests/stdlib/fuzz/f100_strview_accessors.cp` | PASS |
| F-079 | `tests/stdlib/fuzz/f100_strview_utf8.cp` | PASS |
| F-080 | `tests/stdlib/fuzz/f100_strview_stale.cp` | PASS |
| F-081 | `tests/stdlib/fuzz/f100_file_write_read.cp` | PASS |
| F-082 | `tests/stdlib/fuzz/f100_file_lines.cp` | PASS |
| F-083 | `tests/stdlib/fuzz/f100_file_lifecycle.cp` | PASS |
| F-084 | `tests/stdlib/fuzz/f100_mem_guarded.cp` | PASS |
| F-085 | `tests/stdlib/fuzz/f100_mem_tokens.cp` | PASS |

**F stdlib thread/JSON (F-086..F-093)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-086 | `tests/stdlib/fuzz/f100_thread_mutex.cp` | PASS |
| F-087 | `tests/stdlib/fuzz/f100_thread_guard.cp` | PASS |
| F-088 | `tests/stdlib/fuzz/f100_thread_condvar.cp` | PASS |
| F-089 | `tests/stdlib/fuzz/f100_json_gen.cp` | PASS |
| F-090 | `tests/stdlib/fuzz/f100_json_getters.cp` | PASS |
| F-091 | `tests/stdlib/fuzz/f100_json_builder.cp` | PASS |
| F-092 | `tests/stdlib/fuzz/f100_json_cycles.cp` | agent-reported PASS; added after last `summary.json` snapshot |
| F-093 | `tests/stdlib/fuzz/f100_json_drop.cp` | agent-reported PASS; added after last `summary.json` snapshot |

**G stdlib fs/time/net/error/log (F-094..F-100)**

| As-built ID | Exact source path | Last runner evidence |
| --- | --- | --- |
| F-094 | `tests/stdlib/fuzz/f100_fs_pathgen.cp` | PASS |
| F-095 | `tests/stdlib/fuzz/f100_fs_cleanup.cp` | PASS |
| F-096 | `tests/stdlib/fuzz/f100_time_iso8601.cp` | PASS |
| F-097 | `tests/stdlib/fuzz/f100_net_peers.cp` | PASS |
| F-098 | `tests/stdlib/fuzz/f100_net_partial.cp` | PASS |
| F-099 | `tests/stdlib/fuzz/f100_error_obj.cp` | PASS |
| F-100 | `tests/stdlib/fuzz/f100_log_lines.cp` | PASS |

Owning notes files (API intent, seeds, digests, limitations): `tests/crt/fuzz/F100_MEMORY_STRING_NOTES.md`
(F-001..F-020), `tests/crt/fuzz/F100_ALLOC_LIFE_NOTES.md` (F-021..F-040),
`tests/crt/fuzz/F100_FORMAT_IO_NOTES.md` (F-041..F-060),
`tests/stdlib/fuzz/F100_CONTAINERS_NOTES.md` (F-061..F-075),
`tests/stdlib/fuzz/F100_STRING_FILE_NOTES.md` (F-076..F-085),
`tests/stdlib/fuzz/F100_THREAD_JSON_NOTES.md` (F-086..F-093),
`tests/stdlib/fuzz/F100_SYSTEM_NOTES.md` (F-094..F-100).

## 2. Invalid-input policy legend (applies to every F-config)

Each configuration below carries one of these policies:

- **P1 PRECONDITION-SAFE** — unchecked C APIs (`memcpy`, `strcpy`, `printf`,
  file I/O). Generate only valid pointers, in-bounds `n`, NUL-terminated
  strings, nonzero divisors. Passing raw OOB pointers / huge `n` and reporting
  a crash is an invalid bug claim. Such cases belong to a separate
  forked-child fatal-handler test, not to the fuzz corpus.
- **P2 CONTRACT-DRIVEN** — checked `_s` APIs that document an `errno_t` for
  NULL pointers, `n > dmax`, `dmax == 0`, `SIZE_MAX`, interior pointers,
  double free, double close. These inputs are legitimate *because the contract
  defines the outcome*: assert the documented error code **and** that no write
  escaped the declared bound (canary check). A crash here is a real finding.
- **P3 RECOVERABLE-ONLY** — C+ `.hp` APIs. Only valid operations; the only
  "failures" generated are documented recoverable results (absent key `-1`,
  `field()` past end, `at_checked()` guard, EOF). Everything is checked against
  an independent shadow model (`docs/stdlib/HP_FUZZ_NOTES.md` §4).
- **P4 MUTATION** — libFuzzer/corpus byte streams. The target must tolerate
  contract-violating bytes; a crash is a finding only if the target's own
  contract allows those bytes. Corpus seeds must be committed and the fallback
  xorshift seed printed.
- **P5 SUBPROCESS-ONLY** — deliberately fatal checks are run in a forked child
  under `timeout`, with stdout/stderr/exit code captured separately. Never in
  the deterministic digest process.

Seed strategy values: **S-FIXED** = fixed LCG/xorshift constant named below;
**S-BYTES** = first input bytes drive sizes/ops; **S-CORPUS** = committed corpus
files plus mutation; **S-BOUNDARY** = seed schedule forces 0 / exact-fit /
`SIZE_MAX` / one-past-end; **S-INVALID** = partition of documented recoverable
errors; **S-SCHEDULE** = fixed op-kind mix over a bounded domain.

## 3. Numbered plan: F001–F100

Field legend: **APIs**, **Seed**, **Invalid input** (P-code), **Status** with
cited path for EXISTING. "Call-only oracle" means the harness exercises the API
and checks "no crash" but does not verify a precise postcondition.

### Family A — memory ops (8)

**F001 — Unchecked `memcpy` distinct buffers**
- APIs: `cplus_memcpy`
- Seed: S-BOUNDARY; LCG `0x5EED1234`, forces `n=0` then `n=256` exact fit
- Invalid input: P1 (distinct guarded objects, in-bounds `n`)
- Status: EXISTING — `tests/libc/fuzz_libc.c`; also `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F002 — Unchecked `memmove` distinct buffers**
- APIs: `cplus_memmove`
- Seed: S-BOUNDARY; same LCG, canary + untouched-tail assertions
- Invalid input: P1
- Status: EXISTING — `tests/libc/fuzz_libc.c`.

**F003 — Unchecked `memmove` overlapping, both directions**
- APIs: `cplus_memmove` with offsets 32/48, lengths ≤ 128
- Seed: S-SCHEDULE; forced on iteration `i % 17 == 2`
- Invalid input: P1 (overlap is defined, offsets stay inside 256-byte payload)
- Status: EXISTING — `tests/libc/fuzz_libc.c` (asserts byte-exact result and untouched regions).

**F004 — Unchecked `memset` exact fill / tail intact**
- APIs: `cplus_memset`
- Seed: S-BOUNDARY; `n ∈ [0,256]`, iteration 1 forces 256
- Invalid input: P1 (fill byte already reduced to `0..255`)
- Status: EXISTING — `tests/libc/fuzz_libc.c`.

**F005 — Unchecked `memchr` found / not-found**
- APIs: `cplus_memchr`
- Seed: S-FIXED; small alphabet `A..D` present, `X..W` absent
- Invalid input: P1 (assert first occurrence index or NULL)
- Status: EXISTING — `tests/libc/fuzz_libc.c`.

**F006 — Unchecked `memrchr` found / not-found**
- APIs: `cplus_memrchr`
- Seed: same as F005
- Invalid input: P1 (assert last occurrence index or NULL)
- Status: EXISTING — `tests/libc/fuzz_libc.c`.

**F007 — Unchecked `memcmp` on randomized buffers**
- APIs: `cplus_memcmp`
- Seed: S-FIXED; LCG `0xC0FFEE`, 1500 iterations
- Invalid input: P1 (both buffers in-bounds)
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only; no sign assertion — strengthening this is a follow-up).

**F008 — Zero-length / one-byte memory-op boundary**
- APIs: all eight of `cplus_mem{cpy,move,set,chr,rchr}`, `cplus_str{nlen,ncpy,ncat}`
- Seed: S-BOUNDARY; iteration 0 forces `n=0`
- Invalid input: P1
- Status: EXISTING — `tests/libc/fuzz_libc.c`.

### Family B — checked memory (8)

**F009 — `memcpy_s` truthful bounds**
- APIs: `cplus_memcpy_s`
- Seed: S-BYTES / S-FIXED LCGs `0xC0FFEE` and `1`
- Invalid input: P2 (`n ≤ dmax`, canary checked after each call)
- Status: EXISTING — `tests/crt/fuzz/fuzz_memcpy_s.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F010 — `memcpy_s` rejection when `n > dmax`**
- APIs: `cplus_memcpy_s`
- Seed: S-INVALID; `dmax`/`smax` drawn independently so `smax > dmax` occurs
- Invalid input: P2 (assert nonzero return and untouched tail)
- Status: EXISTING — `tests/crt/fuzz/fuzz_memcpy_s.c`, `tests/crt/fuzz/fuzz_memory.c`.

**F011 — `memcpy_s` NULL destination under IGNORE mode**
- APIs: `cplus_memcpy_s(NULL, 16, "abc", n)`, `cplus_safe_set_constraint_mode`
- Seed: S-INVALID; alternates NULL/valid every other iteration
- Invalid input: P2 (documented `ESNULLP`-class rejection)
- Status: EXISTING — `tests/crt/fuzz/fuzz_error.c`.

**F012 — `memcpy_s` NULL source / both NULL**
- APIs: `cplus_memcpy_s` with NULL src (and NULL src+dest)
- Seed: S-INVALID partition
- Invalid input: P2 (assert documented error, no write)
- Status: **TO_ADD** — reconciled in §1.4: as-built F-019 `tests/crt/fuzz/f100_str_constraint_matrix.c` probes NULL *dest* for `memcpy_s` and NULL diff for `memcmp_s`, not a NULL source; the out-of-scope `tests/crt/adversarial/test_raii50_fuzz_memcpy_family.c` still does. Not closed.

**F013 — `memmove_s` truthful bounds / SAFECRT level**
- APIs: `cplus_memmove_s`
- Seed: S-BYTES + S-FIXED LCG `1`
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_memory.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F014 — `memset_s` rejection of value > 255**
- APIs: `cplus_memset_s`
- Seed: S-INVALID with `value ∈ {256, 300, INT_MAX}` plus valid `0..255`
- Invalid input: P2 (assert documented `ESLEMAX`-class error and zero writes)
- Status: **TO_ADD** — reconciled in §1.4: as-built F-008 `tests/crt/fuzz/f100_mem_memset_s.c` covers NULL, zero dmax, `n > dmax` and overflow `n`, but still reduces the fill value to a byte; as-built F-019 covers overflow `n` only. No scoped harness passes a value `> 255`. Not closed.

**F015 — `memzero_s` idempotence**
- APIs: `cplus_memzero_s`
- Seed: S-FIXED
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_memory.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F016 — `memcmp_s` diff out-parameter**
- APIs: `cplus_memcmp_s`
- Seed: S-FIXED LCG `0xC0FFEE` and input bytes
- Invalid input: P2 (assert `diff` sign matches host `memcmp` when `n ≤ dmax`)
- Status: EXISTING — `tests/crt/fuzz/fuzz_memory.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

### Family C — string ops (7)

**F017 — `strlen`/`strnlen` on controlled NUL-terminated strings**
- APIs: `cplus_strlen`, `cplus_strnlen`
- Seed: S-FIXED; bounded length `0..64` in a canary-guarded object
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only), `tests/libc/fuzz_libc.c` (asserts `min(n, strlen)`).

**F018 — `strcpy`/`strncpy` with bounded source**
- APIs: `cplus_strcpy`, `cplus_strncpy`
- Seed: S-FIXED LCG `0xC0FFEE`
- Invalid input: P1 (destination capacity ≥ source)
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F019 — `strcat`/`strncat` with bounded prefix**
- APIs: `cplus_strcat`, `cplus_strncat`
- Seed: S-FIXED
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F020 — comparison/search string family**
- APIs: `strcmp`, `strncmp`, `strchr`, `strrchr`, `strstr`
- Seed: S-FIXED
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F021 — case-insensitive string family**
- APIs: `strcasecmp`, `strncasecmp`, `strcasestr`
- Seed: S-FIXED
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`, `tests/libc/fuzz_libc2.c`.

**F022 — string duplication family**
- APIs: `strdup`, `strndup` (including `n == 0`)
- Seed: S-FIXED; `strndup` asserts length/prefix/NUL in `fuzz_libc2.c`
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`, `tests/libc/fuzz_libc2.c`.

**F023 — tokenizing and search extension family**
- APIs: `strtok_r`, `strsep`, `strchrnul`, `strverscmp`
- Seed: S-FIXED; comma-delimited grammar, reflexivity/antisymmetry asserted
- Invalid input: P1
- Status: EXISTING — `tests/libc/fuzz_libc2.c`.

### Family D — checked strings (7)

**F024 — `strcpy_s` with bounded `dmax`**
- APIs: `cplus_strcpy_s`
- Seed: S-BYTES + LCG `1`
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_strcpy_s.c`, `tests/crt/fuzz/fuzz_strings.c`.

**F025 — `strncpy_s` with `n` crossing `dmax`**
- APIs: `cplus_strncpy_s`
- Seed: S-BYTES
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_strcpy_s.c`, `tests/crt/fuzz/fuzz_strings.c`.

**F026 — checked concatenation family**
- APIs: `cplus_strcat_s`, `cplus_strncat_s`
- Seed: S-BYTES + LCG `1`
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_strings.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F027 — `strcmp_s` indicator out-parameter**
- APIs: `cplus_strcmp_s`
- Seed: S-BYTES
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_strings.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F028 — `strnlen_s` bounded length**
- APIs: `cplus_strnlen_s`
- Seed: S-BYTES
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_strings.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F029 — `strtok_s` reentrant tokenization**
- APIs: `cplus_strtok_s`
- Seed: S-BYTES; delimiters `,;:\t `, 127-byte buffer
- Invalid input: P2 (documented recoverable end-of-tokens)
- Status: EXISTING — `tests/crt/fuzz/fuzz_parsing.c`.

**F030 — wide-string core under C locale**
- APIs: `cplus_wcslen`, `cplus_wcscpy`, `cplus_wcscmp`
- Seed: S-FIXED; bounded `0..13` wchar strings
- Invalid input: P1 (NUL-terminated, capacity 16)
- Status: EXISTING — `tests/libc/fuzz_libc2.c`.

### Family E — format (6)

**F031 — `snprintf_s` with neutralized format bytes**
- APIs: `cplus_snprintf_s`
- Seed: S-BYTES with every `%` rewritten to `_` (31-byte cap)
- Invalid input: P1 (format is controlled; no `%n`)
- Status: EXISTING — `tests/crt/fuzz/fuzz_format.c`, `tests/crt/fuzz/fuzz_format_s.c`.

**F032 — `sprintf_s` with fixed-size destination**
- APIs: `cplus_sprintf_s`
- Seed: S-BYTES / LCG `1`
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_format.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F033 — variadic wrappers `vsnprintf_s` / `vsprintf_s`**
- APIs: `cplus_vsnprintf_s`, `cplus_vsprintf_s`
- Seed: S-FIXED; fixed formats `"%d"`, `"%s:%zu"`
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (`vsn_s_wrap`, `vs_s_wrap`), `tests/crt/fuzz/fuzz_all_safe_apis.c` (`vsn`).

**F034 — unchecked `snprintf`/`vsnprintf`**
- APIs: `cplus_snprintf`, `cplus_vsnprintf`
- Seed: S-FIXED; `i` as `%d`
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F035 — printf family with literal formats**
- APIs: `cplus_printf`, `cplus_printf_s`, `cplus_fprintf_s`
- Seed: S-FIXED
- Invalid input: P1 (fixed `"%s"`/`"%d"` formats)
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`.

**F036 — raw format-string bytes including `%n`**
- APIs: `snprintf`-family with attacker-controlled format bytes
- Seed: S-CORPUS + S-INVALID (`%n`, `%s` with missing args, huge width/precision)
- Invalid input: P5 (run in a forked child under `timeout`; only count a finding if the wrapper's documented contract guarantees rejection — never report the C runtime's own UB as a library bug)
- Status: EXISTING — reconciled in §1.4: as-built F-050 `tests/crt/fuzz/f100_fmt_contract.c` exercises `%n`, non-literal and NULL format/dest refusal (PASS in last runner snapshot). Pre-f100 harnesses still neutralize `%`. Caveat: this is a checked-API refusal oracle, not raw C-runtime format UB under P5.

### Family F — I/O (6)

**F037 — `fread_s`/`fwrite_s` round trip**
- APIs: `cplus_fwrite_s`, `cplus_fread_s`, `rewind`
- Seed: S-BYTES; read back `min(size, 64)` bytes
- Invalid input: P1 (count bounded by buffer)
- Status: EXISTING — `tests/crt/fuzz/fuzz_stdio.c`.

**F038 — `fopen_s`/`fclose_s` happy path**
- APIs: `cplus_fopen_s`, `cplus_fclose_s`
- Seed: S-BYTES
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_stdio.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F039 — double-close recovery**
- APIs: `cplus_fclose_s` called twice on the same `FILE*`
- Seed: S-FIXED
- Invalid input: P2 (documented recoverable outcome asserted)
- Status: EXISTING — `tests/crt/fuzz/fuzz_lifecycle.c`.

**F040 — stream state / positioning family**
- APIs: `cplus_fseek`, `cplus_ftell`, `cplus_fflush`, `cplus_feof`, `cplus_ferror`, `cplus_clearerr`
- Seed: S-FIXED; `i % 16` rotates the file path
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F041 — character I/O family**
- APIs: `cplus_fgets`, `cplus_fputs`, `cplus_getc`, `cplus_putc`
- Seed: S-FIXED
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F042 — formatted input / EOF readers**
- APIs: `cplus_fscanf_s`, `read_int`, `read_long`, `read_double`, `read_line`, `stdlib_io_token`
- Seed: S-FIXED (`/dev/null` stdin for the `.hp` readers)
- Invalid input: P3 (EOF is documented and error stays OK)
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`, `tests/stdlib/fuzz/fuzz_hp.cp`.

### Family G — files (6)

**F043 — remove/rename lifecycle**
- APIs: `cplus_remove`, `cplus_rename`
- Seed: S-FIXED; `path`/`path2` rotate over 16 slots
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`.

**F044 — fixed temporary-path open/close churn**
- APIs: `fopen_s`/`fclose_s` on `/tmp/cplus_fuzz_*.tmp`
- Seed: S-FIXED (`i % 16`)
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_stdio.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F045 — invalid path / permission-denied open**
- APIs: `fopen_s`, `remove`, `rename` with nonexistent directory, directory-as-file, read-only file
- Seed: S-INVALID path partition
- Invalid input: P2 (assert documented error and no partial handle)
- Status: EXISTING — reconciled in §1.4: as-built F-094 `tests/stdlib/fuzz/f100_fs_pathgen.cp` drives empty/NULL/embedded-NUL/over-long (PATH_MAX+128) paths and bad `XXXXXX` templates through fallible `fs_*` helpers; F-083 `tests/stdlib/fuzz/f100_file_lifecycle.cp` covers invalid file handles. Partial: `fopen_s`-specific permission-denied on a read-only file is not isolated.

**F046 — over-long / embedded-NUL / traversal paths**
- APIs: stdlib fs path APIs (`path` validation), `fopen_s`
- Seed: S-BOUNDARY (`PATH_MAX+128`, embedded `\0`, `..` segments, empty string)
- Invalid input: P2/P5
- Status: EXISTING — reconciled in §1.4: as-built F-094 `tests/stdlib/fuzz/f100_fs_pathgen.cp` generates empty, NULL, embedded-NUL and over-long (PATH_MAX+128) path strings plus bad templates; PASS in last runner snapshot.

**F047 — concurrent open/rename/remove on one path**
- APIs: file lifecycle under threads
- Seed: S-SCHEDULE with two threads on a shared path
- Invalid input: P1 (only valid operations, interleaving is the fuzzed variable)
- Status: **TO_ADD** — reconciled in §1.4: F-086..F-088 add real bounded threads but only on mutex/guard/condvar lifecycles; F-095 `f100_fs_cleanup.cp` is sequential. No concurrent file-path harness exists. Not closed.

**F048 — file-descriptor exhaustion recovery**
- APIs: `fopen_s` repeated until failure, then `fclose_s` all and retry
- Seed: S-BOUNDARY
- Invalid input: P2 (exhaustion is a documented recoverable failure)
- Status: **TO_ADD** — reconciled in §1.4: F-097/F-098 check `/dev/fd` leak counts but never drive `fopen_s` to exhaustion. Not closed.

### Family H — allocators (8)

**F049 — malloc/realloc/free basic sequence**
- APIs: `malloc_s`, `realloc_s`, `free_s`
- Seed: S-BYTES / LCG `1` with sizes ≤ 512
- Invalid input: P2 (`NULL` realloc from start, check every return)
- Status: EXISTING — `tests/crt/fuzz/fuzz_alloc_s.c`, `tests/crt/fuzz/fuzz_allocator.c`.

**F050 — calloc_s with small/growing dimensions**
- APIs: `calloc_s`
- Seed: S-BYTES (`data[1]`, `data[3] ? data[3] : 1`) and `%64` dimensions
- Invalid input: P1/P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_alloc_s.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F051 — realloc_s with SIZE_MAX**
- APIs: `realloc_s(p, SIZE_MAX)`
- Seed: S-BOUNDARY; forced on every 7th byte
- Invalid input: P2 (documented rejection; original pointer must stay valid)
- Status: EXISTING — `tests/crt/fuzz/fuzz_realloc.c`.

**F052 — double-free detection**
- APIs: `free_checked_s` twice on one pointer
- Seed: S-INVALID (`data[i] % 4 == 0`)
- Invalid input: P2 (documented recoverable error on second free)
- Status: EXISTING — `tests/crt/fuzz/fuzz_free.c`.

**F053 — non-owned / interior pointer free**
- APIs: `free_checked_s(&local)`, `free_checked_s(p + 1)`
- Seed: S-INVALID
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_free.c`.

**F054 — ownership transfer and liveness query**
- APIs: `cplus_safe_transfer_ownership`, `cplus_safe_ptr_is_live`, `free_s`
- Seed: S-BYTES with odd/even free decision
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_ownership.c`.

**F055 — calloc overflow near SIZE_MAX**
- APIs: `calloc_s(a, b)` where `a*b` wraps
- Seed: S-BOUNDARY (`SIZE_MAX`, `SIZE_MAX/2+1`, `1<<62`)
- Invalid input: P2 (assert overflow rejection, no allocation, no write)
- Status: EXISTING — reconciled in §1.4: as-built F-022 `tests/crt/fuzz/f100_alloc_f022_calloc.c` drives count·size products at the overflow edge (`SIZE_MAX`, `SIZE_MAX/2`, `RSIZE_MAX+1`) and asserts `EOVERFLOW` refusal; PASS in last runner snapshot.

**F056 — allocator metadata / canary corruption detection**
- APIs: allocation header/footer integrity checks around fuzzed writes
- Seed: S-BYTES + deliberate single-byte overwrite in a child
- Invalid input: P5 (corruption check must be asserted via a guard, not inferred from a crash)
- Status: EXISTING — reconciled in §1.4: as-built F-029 `tests/crt/fuzz/f100_alloc_f029_canary.c` and F-039 `tests/crt/fuzz/f100_life_f039_canary.c` assert tracked-size canary integrity and mmap guard-page detection in forked children (P5 discipline); F-026/F-035 cover side-table metadata. PASS in last runner snapshot.

### Family I — containers (10)

**F057 — intmap put/get/contains/remove**
- APIs: `intmap` family with a 32-key domain
- Seed: S-SCHEDULE; LCG `0xC0FFEE`, key = `r % 32`
- Invalid input: P3 (absent key returns `-1`, checked against shadow)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_containers.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F058 — intset insert/remove/contains**
- APIs: `intset` family
- Seed: S-SCHEDULE, 32-key domain
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_containers.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F059 — strmap owned-key put/get/remove**
- APIs: `strmap` family
- Seed: S-SCHEDULE; keys `s0..s31` from LCG
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_hp.cp`.

**F060 — deque both-end push/pop + checked read**
- APIs: `deque.push_back/push_front/pop_front/pop_back/at_checked`
- Seed: S-SCHEDULE; extent capped at 48
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_containers.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F061 — list both-end push/pop + checked read**
- APIs: `list` family
- Seed: S-SCHEDULE
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_containers.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F062 — stack push/pop/top_checked**
- APIs: `stack` family
- Seed: S-SCHEDULE
- Invalid input: P3 (empty-stack checked read folded into digest, never dereferenced)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_containers.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F063 — queue push/pop/front_checked**
- APIs: `queue` family
- Seed: S-SCHEDULE
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_containers.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F064 — clear/grow/drop idempotence**
- APIs: `clear`, `grow`, `drop` across all nine containers
- Seed: S-SCHEDULE; called at capacity boundaries and once at exit
- Invalid input: P3 (`drop()` idempotent per notes)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_hp.cp`.

**F065 — allocation-failure injection during container growth**
- APIs: container growth paths with a fail-Nth-allocation allocator hook
- Seed: S-INVALID; fail at each of the first N growths, N bounded
- Invalid input: P3 (every injection must produce a documented recoverable result with an unchanged shadow model)
- Status: EXISTING — reconciled in §1.4: as-built F-073 `tests/stdlib/fuzz/f100_vec_f073_faults.cp` and F-074 `tests/stdlib/fuzz/f100_cont_f074_faults.cp` inject `stdlib_alloc_fail_at(1)` on guaranteed allocation paths and assert recoverable `-1` with an unchanged shadow model; F-028/F-037 cover SafeCRT/libc allocation injection. PASS in last runner snapshot.

**F066 — hash worst-case / deep collision stream**
- APIs: `intmap`, `strmap` insert with adversarial key distribution
- Seed: S-BOUNDARY; keys chosen to collide under the live hash
- Invalid input: P3
- Status: EXISTING — reconciled in §1.4: as-built F-061 `tests/stdlib/fuzz/f100_cont_f061_collide_int.cp` and F-064 `tests/stdlib/fuzz/f100_cont_f064_collide_strmap.cp` build 128 distinct collision-heavy keys using the public inline hash helpers; PASS in last runner snapshot.

### Family J — string/vector (8)

**F067 — string append/clear bounded by MAXSTR**
- APIs: `string.append`, `string.clear`
- Seed: S-SCHEDULE; cap 64 bytes, clear-and-restart
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_string.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F068 — string reserve/substr/field/at/find**
- APIs: `string.reserve`, `substr`, `field`, `at`, `find`
- Seed: S-SCHEDULE; indices reduced to `0..7`, fields `0..3`
- Invalid input: P3 (`field()` past end returns `-1`)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_string.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F069 — string_view size/equals/stale**
- APIs: `to_view`, `string_view.size/equals`, `slice.stale`
- Seed: S-FIXED after each mutation
- Invalid input: P3 (stale view checked, never dereferenced)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_string.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F070 — UTF-8 validity invariant**
- APIs: `string.is_valid_utf8` checked against shadow after every op
- Seed: S-SCHEDULE with bounded alphabet
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_hp.cp` (`check_invariants`).

**F071 — vector push/insert/erase/pop with shadow model**
- APIs: `vector` family
- Seed: S-SCHEDULE; extent capped at 48/1024
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_string.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F072 — vector slice/view + stale detection**
- APIs: `vector.view`, `slice.at_checked`, `slice.stale`
- Seed: S-FIXED after each mutation
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_hp.cp`.

**F073 — vector at_checked just past the end**
- APIs: `vector.at_checked(size())`
- Seed: S-BOUNDARY; index = `r % (size+1)`
- Invalid input: P3 (guarded `-1`, never dereferenced)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_string.cp`, `tests/stdlib/fuzz/fuzz_hp.cp`.

**F074 — arbitrary-byte string content with embedded NUL / invalid UTF-8**
- APIs: `string.append` on raw bytes + UTF-8/NUL invariant oracle
- Seed: S-CORPUS / mutation over full `0..255` byte domain
- Invalid input: P4 (accepted bytes must still yield a defined, checked string state)
- Status: EXISTING — reconciled in §1.4: as-built F-079 `tests/stdlib/fuzz/f100_strview_utf8.cp` validates raw byte arrays against an independent UTF-8 decoder oracle and `string_view::is_valid_utf8`; F-084 `f100_mem_guarded.cp` adds byte-exact guarded-memory checks. PASS in last runner snapshot. Note: the domain is raw bytes, not embedded-NUL string append specifically.

### Family K — thread/lifecycle (6)

**F075 — single-thread allocator lifecycle sequence**
- APIs: `malloc_s` → write → `realloc_s` → `free_s` sequences
- Seed: S-FIXED LCGs `0xC0FFEE`, `1`
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_allocator.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F076 — double-free / double-close recovery in one stream**
- APIs: `free_checked_s`, `cplus_fclose_s`
- Seed: S-INVALID
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_free.c`, `tests/crt/fuzz/fuzz_lifecycle.c`.

**F077 — constraint-mode mutation during calls**
- APIs: `cplus_safe_set_constraint_mode` interleaved with `memcpy_s`
- Seed: S-INVALID (`data[i] % 5`)
- Invalid input: P2 (mode always reset to IGNORE for the next call)
- Status: EXISTING — `tests/crt/fuzz/fuzz_error.c`.

**F078 — concurrent allocator free/transfer from multiple threads**
- APIs: `malloc_s`, `realloc_s`, `free_s`, `cplus_safe_transfer_ownership`
- Seed: S-SCHEDULE with 2–4 threads on shared pointers
- Invalid input: P1/P2; TSan build required
- Status: **TO_ADD** — reconciled in §1.4: F-086..F-088 now create bounded worker threads, but they exercise mutex/guard/condvar lifecycles, not concurrent allocator free/transfer on shared pointers. Not closed.

**F079 — concurrent container access under TSan**
- APIs: `intmap`, `vector`, `queue` from multiple threads
- Seed: S-SCHEDULE with barrier-synchronized interleavings
- Invalid input: P3; TSan build required
- Status: **TO_ADD** — reconciled in §1.4: F-086..F-088 use barrier-style handshakes but no TSan build and no concurrent `intmap`/`vector`/`queue` access. Not closed.

**F080 — logging enable/disable parity across threads**
- APIs: `cplus_libc_log_set_enabled`, digest parity
- Seed: S-FIXED `0x5EED1234`
- Invalid input: P1
- Status: **TO_ADD** — reconciled in §1.4: as-built F-020 `tests/crt/fuzz/f100_str_log_parity.c` still proves OFF/ON digest parity single-threaded only. Not closed.

### Family L — JSON (6)

**F081 — well-formed object parse**
- APIs: `json(const char*, error*)`, `json_view.size/at`
- Seed: S-CORPUS `json-1` = `{"a":1}` plus mutation
- Invalid input: P4 (target tolerates arbitrary bytes)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_json.cp` with `tests/stdlib/fuzz/corpus/json-1`.

**F082 — array parse**
- APIs: same, `json-2` = `[]`
- Seed: S-CORPUS
- Invalid input: P4
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_json.cp` with `tests/stdlib/fuzz/corpus/json-2`.

**F083 — arbitrary-byte JSON mutation**
- APIs: JSON parser on the full byte domain
- Seed: S-CORPUS + xorshift `0x9e3779b97f4a7c15` fallback (`fuzz_main.c`), 200/500 runs
- Invalid input: P4
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_json.cp`, `tests/stdlib/fuzz/fuzz_main.c`.

**F084 — get_int type mismatch on a valid document**
- APIs: `json_view.get_int(&v, &err)` on non-integer values
- Seed: S-CORPUS mutation to produce strings/bools/null as items
- Invalid input: P3 (assert a documented error category, not a crash)
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_json.cp` calls `get_int` on every root item of every valid document (call-only; explicit error-category assertion is a strengthening item).

**F085 — deeply nested JSON / depth-limit**
- APIs: parser nesting depth, depth-limit error
- Seed: S-BOUNDARY with generated depth 1..N
- Invalid input: P5 (stack exhaustion must be rejected by a documented limit; child under timeout)
- Status: **TO_ADD** — reconciled in §1.4: F-089 `f100_json_gen.cp` generates valid/malformed nested documents but has no explicit depth-limit oracle or 1..N depth schedule; F-092/F-093 (added after the last runner snapshot) cover cycles/drop, not depth. Not closed.

**F086 — JSON number overflow / huge numeric literals**
- APIs: `get_int`, parser numeric conversion
- Seed: S-BOUNDARY (10^400, `INT64_MIN/MAX ± 1`, leading zeros)
- Invalid input: P3 (documented overflow/error result asserted)
- Status: **TO_ADD** — reconciled in §1.4: F-090 `f100_json_getters.cp` asserts wrong-type refusals, not numeric-overflow results; no huge-literal seeds. Not closed.

### Family M — fs/time/net/error/log (7)

**F087 — error value lifecycle**
- APIs: `error()`, `clear()`, `ok()`, `category()`, `code()`
- Seed: S-FIXED (`/dev/null` stdin readers)
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_hp.cp` (`io_check`).

**F088 — constraint-mode switching under load**
- APIs: `cplus_safe_set_constraint_mode` for all five modes
- Seed: S-INVALID (`data[i] % 5`)
- Invalid input: P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_error.c`.

**F089 — statistics snapshot**
- APIs: `cplus_safe_stats_dump`
- Seed: S-BYTES
- Invalid input: P2 (32-entry snapshot buffer, exact size)
- Status: EXISTING — `tests/crt/fuzz/fuzz_error.c`.

**F090 — safe-log level and emission**
- APIs: `cplus_safe_log_set_level`, `cplus_safe_log`, `cplus_safe_platform_probe`
- Seed: S-FIXED
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_realloc.c`, `tests/crt/fuzz/fuzz_free.c`, `tests/crt/fuzz/fuzz_all_exported.c`.

**F091 — libc logging on/off digest parity**
- APIs: `cplus_libc_log_set_enabled` + all eight memory/string APIs
- Seed: S-FIXED `0x5EED1234`, state reset per pass
- Invalid input: P1
- Status: EXISTING — `tests/libc/fuzz_libc.c` (4800 cases, identical digest asserted).

**F092 — platform info probe**
- APIs: `cplus_safe_platform_probe`
- Seed: S-FIXED
- Invalid input: P1
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`.

**F093 — time/strftime + fs metadata + net-address parsing**
- APIs: `asctime`/`strftime`/`localtime`, filesystem stat-style queries, address parsing helpers
- Seed: S-BOUNDARY (extreme `tm` fields, leap dates, malformed address strings)
- Invalid input: P1/P2
- Status: EXISTING — reconciled in §1.4: as-built F-096 `tests/stdlib/fuzz/f100_time_iso8601.cp` (strftime against a `gmtime_r` oracle, 11 fixed dates + 400 seeded), F-097 `f100_net_peers.cp` and F-098 `f100_net_partial.cp` (loopback/AF_UNIX, invalid ports), and F-094 `f100_fs_pathgen.cp` (`fs_size`/path queries) cover this surface. Partial: stat-style filesystem metadata is exercised only through `fs_size`, not a full stat oracle.

### Family N — cross-module (7)

**F094 — safe + unsafe memory in one stream**
- APIs: `memcpy` and `memcpy_s` interleaved on related buffers
- Seed: S-FIXED `0xC0FFEE`, 1500 iterations
- Invalid input: P1/P2 mixed per call
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F095 — safe + unsafe string ops in one stream**
- APIs: `strcpy`/`strcat` and `strcpy_s`/`strcat_s` interleaved
- Seed: S-FIXED
- Invalid input: P1/P2 mixed
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c` (call-only).

**F096 — allocator + file I/O + format in one stream**
- APIs: `malloc_s`/`free_s`, `fopen_s`/`fwrite_s`/`fscanf_s`, `snprintf_s` family
- Seed: S-FIXED LCGs `0xC0FFEE` and `1`
- Invalid input: P1/P2
- Status: EXISTING — `tests/crt/fuzz/fuzz_all_exported.c`, `tests/crt/fuzz/fuzz_all_safe_apis.c`.

**F097 — C harness ↔ C+ `.hp` differential on one seed stream**
- APIs: same logical operations via C safe CRT and C+ `.hp` wrappers; compare outcomes
- Seed: S-FIXED shared LCG
- Invalid input: P1/P3 (both sides must be inside their documented contracts)
- Status: **TO_ADD** — reconciled in §1.4: the corpus adds C↔host-CRT differentials (F-055..F-058) and C+↔shadow-model oracles, but no single process links a C safe-CRT target and a C+ `.hp` target over a shared seed stream. Not closed.

**F098 — all five `.hp` headers under one digest**
- APIs: `error.hp`, `string.hp`, `vector.hp`, `collections.hp`, `io.hp`
- Seed: S-FIXED `0xC0FFEE`; FNV-1a over op index/kind/key/value/result/extents
- Invalid input: P3
- Status: EXISTING — `tests/stdlib/fuzz/fuzz_hp.cp`; contract documented in `docs/stdlib/HP_FUZZ_NOTES.md` §1–2.

**F099 — UBSan + strict-mode differential on the same seed stream**
- APIs: whole of `fuzz_hp.cp` (and `fuzz_libc.c`) built in default / strict-none / strict-UBSan
- Seed: S-FIXED; digest must be byte-identical across modes
- Invalid input: P3
- Status: EXISTING — `docs/stdlib/HP_FUZZ_NOTES.md` §7 (three modes, identical digest), `docs/libc/LIBC_FUZZ_NOTES.md` (plain + UBSan).

**F100 — orchestration, coverage accounting, and 100-config reporting**
- APIs: all of F001–F100, with a single runner enumerating mode × seed × config
- Seed: S-FIXED manifest with printed seed and case count per config
- Invalid input: P5 for fatal partitions; P1/P2/P3/P4 elsewhere
- Status: EXISTING — reconciled in §1.4: `tests/fuzz100/run.py` / `run.sh` (documented in `tests/fuzz100/README.md`) now discovers and builds every `f100_*` source, runs each under a 30 s bound, and emits `build/fuzz100/summary.json` with per-case config, status, exit code and logs. Caveat: orchestration exists but no recorded run reaches 100/100 `PASS`, and success is exit-0 self-report with no expected-output fixtures.

## 4. Roll-up (with denominators)

Statuses below are for the **original plan IDs in §3**, reconciled by API intent
to the as-built registry in §1.4. A count of 90 of 100 EXISTING does not mean
"90 of 100 as-built configs are correct", and it is not a percentage or a safety
claim.

| Family | Configs | EXISTING | TO_ADD |
| --- | --- | --- | --- |
| A memory ops | 8 of 100 | 8 of 8 | 0 of 8 |
| B checked memory | 8 of 100 | 6 of 8 | 2 of 8 (F012, F014) |
| C string ops | 7 of 100 | 7 of 7 | 0 of 7 |
| D checked strings | 7 of 100 | 7 of 7 | 0 of 7 |
| E format | 6 of 100 | 6 of 6 | 0 of 6 |
| F I/O | 6 of 100 | 6 of 6 | 0 of 6 |
| G files | 6 of 100 | 4 of 6 | 2 of 6 (F047, F048) |
| H allocators | 8 of 100 | 8 of 8 | 0 of 8 |
| I containers | 10 of 100 | 10 of 10 | 0 of 10 |
| J string/vector | 8 of 100 | 8 of 8 | 0 of 8 |
| K thread/lifecycle | 6 of 100 | 3 of 6 | 3 of 6 (F078–F080) |
| L JSON | 6 of 100 | 4 of 6 | 2 of 6 (F085, F086) |
| M fs/time/net/error/log | 7 of 100 | 7 of 7 | 0 of 7 |
| N cross-module | 7 of 100 | 6 of 7 | 1 of 7 (F097) |
| **Total** | **100 of 100** | **90 of 100** | **10 of 100** |

Ten plan IDs remain TO_ADD: F012, F014, F047, F048, F078, F079, F080, F085,
F086, F097. These are reconciled in §3 with the exact as-built paths that come
closest and why they do not close the intent.

Cross-cutting caveats that qualify the 90 of 100 figure:

- The pre-f100 baseline harnesses inventoried in §1.2 are still mostly
  single-input smokes: 15 of 23 are libFuzzer entry points, and 15 of 15 of the
  CRT ones run one fixed input per CI run (4 via `fuzz_smoke.c`, 11 via
  `CPLUS_FUZZ_SMOKE_MAIN`). Only the 3 C+ stdlib targets receive a mutation
  budget (200 in `run_all.sh`, 500 by default).
- The 100 as-built `f100_*` configs are different: deterministic fixed-seed
  drivers with bounded operation budgets (typically 1,200–5,200 ops) and, in
  many families, independent shadow models. They are still sampled schedules,
  not coverage-guided fuzzing, and ASan is unavailable on this host.
- 5 of the 23 baseline harnesses call the target with no precise postcondition
  (F007, F017–F020, F034–F035, F094–F095 are marked call-only above).
- 2 of 23 baseline harnesses have no automated runner (#16 `fuzz_all_exported.c`,
  #23 `fuzz_hp.cp`; #23 is manual per its notes). The as-built corpus does have
  `tests/fuzz100/run.py`, but no recorded run reaches 100/100 config `PASS`.
- No completion percentage is derived from these counts. C1 (27 MISSING ISO C
  APIs) and C2 (49 UNSUPPORTED entries) are unchanged by fuzz activity.

## 5. Top remaining gaps (ranked, after §1.4 reconciliation)

1. **Concurrent allocator/container/path and data-race coverage — F047, F078,
   F079.** As-built F-086..F-088 now create bounded worker threads, but only for
   mutex/guard/condvar lifecycles; no TSan build, no concurrent allocator
   free/transfer on shared pointers, no concurrent file-path operations, and no
   concurrent container access. Highest-value addition: a 2-4 thread
   allocator/container/path fuzzer with a deterministic interleaving schedule
   and a TSan mode.
2. **Logging parity across threads — F080.** As-built F-020
   `tests/crt/fuzz/f100_str_log_parity.c` proves OFF/ON digest parity
   single-threaded only.
3. **Cross-language differential — F097.** The corpus has C-to-host-CRT
   differentials (as-built F-055..F-058) and C+-to-shadow-model oracles, but no
   single process links a C safe-CRT target and a C+ `.hp` target over a shared
   seed stream.
4. **JSON depth and numeric edges — F085, F086.** F-089 `f100_json_gen.cp`
   generates nested valid/malformed documents and F-090 `f100_json_getters.cp`
   checks wrong-type refusals, but there is no explicit nesting-depth limit
   oracle or huge-numeric-literal (`10^400`, `INT64_MIN/MAX +/- 1`) partition.
5. **Alloc-side checked-input edges — F012, F014.** No scoped harness passes a
   NULL *source* to `memcpy_s` or a fill value `> 255` to `memset_s`; the
   as-built constraint matrices cover NULL *dest*, zero dmax and overflow `n`.
6. **File-descriptor exhaustion — F048.** F-097/F-098 check `/dev/fd` leak
   counts but never drive `fopen_s` to exhaustion and recover.
7. **100/100 runner record and orchestration proof.** The runner now exists
   (`tests/fuzz100/run.py`) and produces per-case records, but every recorded
   snapshot is below 100 `PASS` and `SKIPPED` support files are not configs. A
   single completed run with all 100 config sources `PASS` is still absent.

### Closed since the original catalog (audit trail)

F036 (`%n` refusal -> F-050), F045/F046 (invalid paths -> F-094), F055 (calloc
overflow -> F-022), F056 (metadata/canary -> F-029/F-039), F065 (injection ->
F-073/F-074), F066 (hash collisions -> F-061/F-064), F074 (raw-byte UTF-8 ->
F-079), F093 (time/net/fs -> F-096/F-097/F-098/F-094), F100 (orchestration ->
`tests/fuzz100/run.py`). See §1.4 for exact paths and runner evidence.

## 6. Suggested execution order for remaining TO_ADD work

1. Add F078-F080 and F047 with a forked-child/TSan discipline; this is the
   largest remaining zero-coverage family.
2. Add F085-F086 (JSON depth/numeric limits) and F097 (C-to-C+ differential).
3. Add F012/F014 (checked-input edges) and F048 (FD exhaustion) with bounded,
   non-blocking operations and a private `mkdtemp` sandbox.
4. Re-run `sh tests/fuzz100/run.sh` after sibling agents finish wiring sources
   into build scripts, then re-read `build/fuzz100/summary.json` and re-audit
   §1.4. Only a completed all-config PASS record can change C4.
5. Re-audit C1/C2 separately. The fuzz corpus does not close the 27 MISSING or
   49 UNSUPPORTED ISO C API entries; a closure agent must supply per-API
   evidence before those counts change.

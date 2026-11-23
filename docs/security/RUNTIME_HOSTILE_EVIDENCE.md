# Runtime hostile-input evidence — consolidated ledger

Branch `agent/modern-stdlib`. Evidence collected/recorded on 2026-11-23 on macOS
arm64 (Apple clang 16.0.0 backend where pinned; Homebrew clang 23.1.6 where
explicitly noted). This document **consolidates numbers from existing artifacts
only**; it does not re-run suites, and it does not fabricate results. All counts
below are copied from the cited files. Where an artifact is absent, the row is
marked **PENDING** rather than filled in.

> **Boundary statement (read first).** This is **evidence of defined
> refusal/result behaviour under the specific deterministic hostile inputs
> listed here**, observed with canaries/sentinels intact. It is **NOT a proof of
> full memory safety**, lifetime safety, thread safety, formal correctness, or
> "100% security". It cannot establish absence of defects outside the generated
> input space, and it is not a live-game/injected-DLL result. Sanitizer status is
> limited exactly as recorded in §8.

---

## 1. Headline counts

| Suite | Headline |
|---|---|
| `tests/adversarial_1000` | **8 groups, 8,000 scenarios, 5,209 refusals, 0 failures, exit 0** (target was 9 groups / 9,000 — see §10) |
| `tests/expanded_examples_adversarial` | **110 cases, 110 refusals, 0 crashes, 0 unexpected-OK, exit 0** |
| `tests/abi/stdc_safety` | **`STDC_SAFETY 64 48 64 0`** — 64 cases, 48 refusals, 64 intact canaries, 0 failures, checked exit 0 |
| `tests/security/hostile_inputs` | **143 checks × 2 modes, 0 failures** (strict-none 4/4, workaround-UBSan 4/4, 1 BLOCKED canonical-UBSan, runner exit 2) |
| `tests/security/resource_exhaustion` | **16 case/mode pairs, 78 checks per mode × 2 modes = 156 executions, 0 failures, exit 0**, 1 declared SKIP |
| `tests/security/stdlib` (aggregate runner) | **15 invocations, 15 PASS, 0 FAIL/BUILD_FAILURE/TIMEOUT/BLOCKED, exit 0** |
| `tests/scenarios/cve_like` | **15 baseline CVE-like cases documented; safe-variant gate `10 passed, 0 failed`, exit 0** (earlier state `4 passed, 0 failed`) |
| `examples/screenshots/MANIFEST.md` | **6 real captured transcripts, 6 builds, 7 binary runs, 0 failures, exit 0**, all `VERDICT: PASS` |

Heterogeneous units (scenarios, checks, cases, transcripts) are intentionally
**not** summed into a single grand total; each suite defines its own unit.

---

## 2. Per-suite table

"Integrity" is the suite's own post-call guard check; it is the independent
negative control for each refusal. "Exit" is the authoritative runner/binary
exit code recorded in the artifact.

| Suite (authoritative artifact) | Cases / checks | Refusals | Integrity | Failures / crashes | Exit |
|---|---:|---:|---|---:|---:|
| ADV1000 aggregate (`tests/adversarial_1000/RESULTS.md`) | 8,000 scenarios | 5,209 | full integrity in all 8 groups (see breakdown) | 0 | 0 |
| Expanded examples (`tests/expanded_examples_adversarial/RESULTS.md`) | 110 cases | 110 | canary/sentinel tokens where exposed; required tokens present | 0 crashes, 0 unexpected-OK | 0 |
| stdc_safety checked (`tests/abi/stdc_safety/RESULTS.md`) | 64 | 48 | 64/64 canaries intact | 0 | 0 |
| stdc_safety plain control (`tests/abi/stdc_safety/RESULTS.md`) | 64 | n/a (control) | **13 canaries corrupted** | **23 crashes/abnormal + 19 divergent** | control subprocess |
| stdc_safety UBSan build (`tests/abi/stdc_safety/RESULTS.md`) | 64 | 48 | 64/64 | 0 (`0` `runtime error` reports) | 0 |
| hostile_inputs (`tests/security/hostile_inputs/NOTES.md`) | 143 (strict-none) + 143 (workaround UBSan) | n/a (checks, not refusals) | 0 failures per mode; zero stderr bytes under UBSan | 0 | 2 (1 BLOCKED mode, no failures) |
| resource_exhaustion (`tests/security/resource_exhaustion/NOTES.md`, `.reout/summary.json`) | 8 harnesses × 2 modes; 78 checks/mode (156 executions) | n/a (checks) | all stderr empty; 1 declared SKIP (never a pass) | 0 PASS 16 / FAIL 0 | 0 |
| security aggregate (`build/security/security_summary.json`, `security_table.txt`) | 15 invocations (stdlib 2, hostile_inputs 5, resource_exhaustion 8) | n/a | — | PASS 15, all other statuses 0 | 0 |
| fs_security (`tests/security/stdlib/FS_SECURITY_NOTES.md`) | `FS_SECURITY 68 0` (68 checks) | characterization, not refusals | 68 `FSQ_` lines all `1`; 0 stderr bytes | 0 | 0 |
| thread_security (`tests/security/stdlib/THREAD_SECURITY_NOTES.md`) | 37 checks | n/a | 37 pass / 0 fail | 0 | 0 |
| cve_like safe-variant gate (`tests/scenarios/cve_like/NOTES_5.md`) | 15 baseline cases; gate output lists 10 safe-variant runs | caller-side refusals | no ASan/UBSan diagnostics in safe variants | 0 | 0 |
| screenshot captures (`examples/screenshots/MANIFEST.md`) | 6 transcripts / 7 runs | per-transcript refusal tokens | every transcript `VERDICT: PASS` | 0 | 0 |

### 2.1 ADV1000 per-group breakdown (verbatim final lines)

```
ADV1000 alloc         1000 644 1000 1000 0
ADV1000 containers    1000 614 1000 0
ADV1000 fmt_io        1000 654 1000 0
ADV1000 json_log      1000 626 1000 0
ADV1000 memstr        1000 541 1000 0
ADV1000 multimedia    1000 720 sentinel_ok 0
ADV1000 net_http      1000 660 1000 0
ADV1000 thread_async  1000 750 1000 0
TOTAL                 8000 5209              0
```

| Group | Scenarios | Refused | Integrity field | Failures |
|---|---:|---:|---|---:|
| alloc | 1000 | 644 | `canary_ok=1000`, `state_ok=1000` (`1000/1000`) | 0 |
| containers | 1000 | 614 | `canary_ok=1000` | 0 |
| fmt_io | 1000 | 654 | `canary_ok=1000` | 0 |
| json_log | 1000 | 626 | `sentinel_ok=1000` | 0 |
| memstr | 1000 | 541 | `canary_ok=1000` | 0 |
| multimedia | 1000 | 720 | literal `sentinel_ok` token (all guards intact) | 0 |
| net_http | 1000 | 660 | `sentinel_ok=1000` | 0 |
| thread_async | 1000 | 750 | `state_ok=1000` (canary + metadata) | 0 |
| **TOTAL** | **8000** | **5209** | — | **0** |

Aggregate line: `adv1000-agg: groups=8 scenarios=8000 (target 9000) refused=5209 failures=0 hard_fail=0`.
`aggregate.stderr` was empty; wall clock 2026-11-23 05:51:59Z → 05:52:08Z.

### 2.2 Expanded-examples per-category breakdown

```
ADV_EXPANDED_SUBTOTAL cli       cases=34 refused=34 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED_SUBTOTAL games     cases=26 refused=26 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED_SUBTOTAL database  cases=25 refused=25 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED_SUBTOTAL engine    cases=13 refused=13 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED_SUBTOTAL safety    cases=12 refused=12 crashed=0 unexpected_ok=0 blocked=0
ADV_EXPANDED_TOTAL 110 110 0 0
```

Scope: 18 built targets across `cli`/`games`/`database`/`engine`/`safety`
(4/5/3/3/3). In-flight `games_2`, `games_3`, `games_4`, `apps_2` were explicitly
**out of scope** for these verdicts. Two initially mis-specified cases and one
harness race were disclosed and corrected in `RESULTS.md` (run 4 briefly showed
a false `59 59 0 0` due to a shared-log truncation; run 5 and `final.out` are the
clean, uncontended 110/110 records).

### 2.3 Resource-exhaustion harness counts (both modes byte-identical)

| Harness | Checks | Failures | Notes |
|---|---:|---:|---|
| `re_mem_pressure` | 17 | 0 | allocation-failure injection, no host exhaustion |
| `re_fd_exhaust` | 5 | 0 | EMFILE in child with soft `RLIMIT_NOFILE` |
| `re_huge_collections` | 21 | 0 | pinned `STDLIB_ALLOC_LIMIT=4096` |
| `re_json_nesting` | 7 | 0 | 1024 balanced / 4096 unterminated levels, watched child |
| `re_string_growth` | 8 | 0 | bounded append + injected growth rollback |
| `re_thread_failure` | 7 | 0 | 1 in-test **SKIP** (`pthread_create` not injectable via `RLIMIT_NPROC`) |
| `re_socket_exhaust` | 7 | 0 | AF_UNIX socketpair exhaustion + one loopback TCP exchange |
| `re_malformed_guard` | 6 | 0 | includes **watchdog positive control** |
| **Total per mode** | **78** | **0** | strict-none and UBSan: 156 executions, 0 failures |

---

## 3. Categories of hostile inputs covered

| Category | Concrete hostile inputs observed in the artifacts | Suites |
|---|---|---|
| NULL / missing operands | `NULL` dest/src/format/stream/buffer/path/mode/key/value/handle; `json(NULL)`, zero handles, `net_conn_read_null_buf`, `fs_*_null` | ADV1000 (fmt_io, json_log, memstr, multimedia, net_http), expanded (34+ cases), stdc_safety, hostile_inputs, cve_like |
| One-over / off-by-one | `n = dmax + 1 + (L % 63)` (up to 63 B past capacity); 17 B into 16 B dest; 25 B into 24 B; `"ABCDEFGH"` into `buf[8]`; one-over files and pixel reads | ADV1000 (fmt_io, memstr, containers), stdc_safety, expanded (`file_tools one_over_file`, `copy_one_over`, `cmp_one_over`), screenshots, cve_like case 12 |
| `SIZE_MAX` / extreme magnitudes | `malloc/realloc/aligned_alloc(SIZE_MAX)`, `calloc(SIZE_MAX,…)`, `strnlen/dmax = SIZE_MAX`, `reserve(SIZE_MAX)`, `SIZE_MAX` length/predicate/path/id, `2^62` `size*nmemb`, `18446744073709551615` | ADV1000 (alloc, containers, fmt_io, json_log, memstr, net_http), stdc_safety, expanded, hostile_inputs, cve_like 03/11 |
| Count/size overflow and truncation | 64→32 truncation (`4294967297`); `rows*cols` element-count overflow; `a->offset + bytes` wrap; `size*nmemb` overflow; signed negative before `size_t` cast (`-1`) | stdc_safety, hostile_inputs (`io_int_overflow`, `json_get_int_overflow`), cve_like 01/02/05/11 |
| Overlap / aliasing | overlapping `memmove`/`memcpy`/`strcpy`/`strncpy` (source inside dest), self-append, self-collision move sequences | ADV1000 memstr (`overlap` rc 404), containers (self-append), expanded (`tictactoe self_collision`, `snake_core`, `snake_cli self_collision_stream`) |
| Malformed JSON | truncated object/array/prefix, unterminated string, embedded NUL, invalid UTF-8, 1 MiB document (524,287 elements), deep nesting 256/4096/16384/16385/20000, 2 MiB document | hostile_inputs, ADV1000 json_log, resource_exhaustion (`re_json_nesting`, `re_malformed_guard`), cve_like |
| Malformed HTTP / network framing / URL | 17 malformed URL templates; overlong URLs 8,000,001–8,001,000 B; CRLF/bare-CR/bare-LF/control/oversized header lines; truncated and chunked-framing edge cases (bad hex, missing terminal chunk, bad CRLF, trailers); bogus/borrowed/invalid fd (`-1`, `0`, `1`, `2`, `999999`); no DNS/external host | ADV1000 net_http (10 families × 100), hostile_inputs (`net_*`), resource_exhaustion (`re_socket_exhaust`) |
| Stale generation / stale token / use-after-release | generation bumped by `stdlib_mem_transfer`/`realloc`; stale borrow keys, stale token, consumed future slot, `uaf_meta` metadata-only queries — stale pointer **never dereferenced** | ADV1000 (containers, thread_async), screenshots (`bounds_query`, `lifetime_refusal`), expanded (`bounds_demo`, `lifetime_demo`), cve_like 04 |
| Double release / double free / double destroy / double close | second `lifetime_owner_release` (`CPLUS_EXIT_DOUBLE_FREE`), double free (`EINVAL`), double `fclose_s`, double `pthread_mutex/cnd_destroy`, double `thrd_join`, double drop/unlock/start/get | ADV1000 (alloc, fmt_io, thread_async), stdc_safety, expanded, screenshots (`sync_double_destroy`, `lifetime_refusal`), cve_like 07 |
| Oversized ~10 KB / huge strings and streams | 10 KB strings, values, direction streams and stdin; 1 MiB string/JSON fixtures; 2 MiB document; 4,096-byte log message; ~1,800-byte header line | expanded (`oversized_value` 10 KB, `oversized_stdin`, `snake_cli`), hostile_inputs, ADV1000 (json_log, fmt_io, net_http), screenshots |
| Invalid encodings / embedded NUL | strict UTF-8 rejection classes, invalid UTF-8 keys/fields, embedded-NUL fixtures (file/JSON/string), binary NUL bytes | hostile_inputs, expanded (`invalid_utf8_key`, `invalid_utf8_field`), ADV1000 json_log |
| Malformed paths / filesystem | empty/missing/overlong paths, truncated path, bad `mkdtemp` suffix/size, path traversal `..`, symlink escape, TOCTOU check→swap→use, unsafe `rename` overwrite | hostile_inputs, expanded (`file_tools`, `text_tools`), fs_security |
| Resource exhaustion | EMFILE/ENFILE, AF_UNIX socketpair exhaustion, allocation-pressure injection, oversized collections/strings, thread-creation failure (declared SKIP), malformed-input hangs under watchdog | resource_exhaustion, expanded, ADV1000 alloc |
| Control-flow / concurrency hazards | OOB jump-table dispatch `d[cmd]()` with `cmd=3`/`99`, user-sized VLA stack clash, arena offset wrap, data race / reset-free race on global `active` | cve_like 09/11/13/15 |
| Forbidden format strings | literal and runtime-built `%n`, `%hhn`, `%lln`, `AB%n` refused **before any conversion**; no real `%n` write executed | ADV1000 fmt_io, stdc_safety (`printf`/`fprintf`) |
| Double/negative/bogus handles and zero-length edge cases | zero-size buffers (`size=0`), zero-length `write` on non-open fd (documented defined result), negative signed length, forged `0xDEADBEEF` free pointer | ADV1000 net_http, hostile_inputs, cve_like 03 |

---

## 4. Canary / sentinel integrity evidence

A refusal is counted as acceptable only when the post-call guard check also
passes; the guard is therefore the negative control, not decoration.

| Suite | Guard design | Recorded result |
|---|---|---|
| ADV1000 alloc | live heap canary bytes + allocation-metadata state checked after each call | `canary_ok=1000`, `state_ok=1000`; pinned breakers ids 1–4 (`SIZE_MAX`, count overflow, injected realloc failure with atomicity, double free) all intact |
| ADV1000 containers | 64-byte global sentinel + in-block prefix/suffix canaries; byte-for-byte check of every byte outside intended write range | `canary_ok=1000`; BREAKING family (142 cases) refused without touching adjacent memory |
| ADV1000 fmt_io | 16-byte prefix + 4 KiB data region + 16-byte postfix (`0x5A`); I/O one-over 16+16 guard struct | `canary_ok=1000`; output verified NUL-terminated within `dmax` |
| ADV1000 json_log | four 32-byte guards around JSON work buffer and log-message buffer; a single changed byte fails the case | `sentinel_ok=1000`, `sentinel_not_ok=0`, `FAIL_lines=0` |
| ADV1000 memstr | canary placed at declared capacity (`dmax`/`cap`) | `canary_ok=1000`; hostile lengths returned 400/402/403/404/406/407 with no write past declared bound |
| ADV1000 multimedia | six byte guards (`0xA5/0x5A/0x3C/0xC3/0x7E/0xE7`) around byte/int/float out-parameters + 128 pre-seeded outputs | literal `sentinel_ok`; all 1000 guards/out-params intact |
| ADV1000 net_http | `0xA5` guards around bounded response/slist/URL buffers | `sentinel_ok=1000`; no guard damage despite malformed framing and overlong URLs |
| ADV1000 thread_async | two-value canary (`0xA11CE15EDEADBEEF`, `0x5EED5A17C0FFEE01`) + `stdlib_state.meta_live` | `state_ok=1000`; live-block count returns to pre-scenario value |
| expanded examples | per-example canaries/summary sentinels required via `ADV_REQUIRE_TOKEN` | `kv_store.post_hostile_integrity` keeps `KV item 0 k v`; `table_query` keeps `TQ count grp 10 1`; `safety_demo` ends `SAFETY FINAL_CANARY ok` / `SAFETY_SUMMARY 5 5 5 0`; `memcpy_s_demo` requires parent `parent_canary OK` |
| stdc_safety | 16-byte `0xA5` canary immediately after each guarded destination in the same stack object | 64/64 intact in checked build; plain control corrupts 13 |
| resource_exhaustion | child exit-code assertions, telemetry arithmetic, `stdlib_alloc_seq()` deltas, watchdog positive control | 0 failures; watchdog control proves the guard is live |
| screenshots | real captured canary lines | `memcpy_s ... canary=0x2a corrupted=1` (unguarded child) vs `canary=0xa5 intact=1` (checked); `SAFETY FINAL_CANARY ok`, `SAFETY_SUMMARY 5 5 5 0` |

---

## 5. Plain-vs-checked contrast

| Contrast | Checked / hardened result | Plain / unguarded result | Source |
|---|---|---|---|
| 16 ordinary-C APIs × 4 cases (`memcpy`, `memmove`, `memset`, `strcpy`, `strncpy`, `strcat`, `strncat`, `printf`, `fprintf`, `fopen`, `fread`, `fwrite`, `malloc`, `calloc`, `realloc`, `free`) | `STDC_SAFETY 64 48 64 0`; every hostile case returns a documented failure (`EINVAL`/`ERANGE`/`EOVERFLOW` or safeclib code); all 64 canaries intact; exit 0 | **13** canaries destroyed; **23** abnormal terminations (SIGSEGV 11, SIGABRT 6, SIGTRAP 5); **19** divergent cases; **all 16 valid cases still pass with canary=1** (ABI/source-compatibility control) | `tests/abi/stdc_safety/RESULTS.md` |
| 9-byte copy into 8-byte field | `cplus_memcpy_s` refuses `ESNOSPC = 406`, parent canary stays `0xa5`; exact-fit copy succeeds (positive control) | forked child corrupts canary `0xa5 → 0x2a`, child exits 9 | `examples/screenshots/captures/unsafe_vs_safe_memcpy.txt` |
| CVE-like baselines (cases 01–15) | safe variants refuse or use defined bounded results, exit 0, no sanitizer diagnostics; generation checks refuse stale aliases; checked free refuses double/invalid free | ASan reports heap-buffer-overflow, negative-size-param, heap-use-after-free, double-free, stack-overflow, stack-buffer-overflow (rc 134; `cmd=99` rc 132 control-flow hijack); TSan reports data race (rc 134) | `tests/scenarios/cve_like/NOTES_1.md` … `NOTES_5.md` |
| Severity honesty | Several CVE-like cases are classified **PARTIAL** or **NOT-FIXABLE-BY-LIBC**: ownership/lifetime bugs (UAF, aliasing, jump-table bounds, thread race) require **caller-side** fixes; the library contributes checked allocation/copy/free and provenance queries only | — | `tests/scenarios/cve_like/NOTES_2.md`, `NOTES_3.md`, `NOTES_4.md`, `NOTES_5.md` |

The stdc_safety plain control is a **separate subprocess**, with each hostile
case in a further `fork()`ed child, so corruption/crashes cannot reach the
supervisor or the checked binary. `-fno-builtin` and `#undef` of macOS fortify
macros in the plain build are deliberate: they make raw-libc corruption
observable instead of turning it into an Apple-specific `__*_chk` abort.
ABI evidence: `nm -gU` shows all checked entry points as plain unmangled `T`
symbols in the two archives and in `out/checked`, with zero `__Z`-prefixed text
symbols; `safe_c.h` is source-level macro rewriting only.

---

## 6. Negative controls — the refusals are real

1. **Independent integrity gates.** Every ADV1000 group requires its
   canary/sentinel/state field to pass *in addition* to the refusal; a single
   changed guard byte fails the case (`SECURITY_LOGS.md` §5). The aggregate
   runner rejects missing/malformed final lines, `scenarios != 1000`, integrity
   below 1000/non-`*_ok`, or `failures > 0` — a timeout is a failure, never a pass.
2. **Forced-failure probes of the harness itself.** The expanded suite proves
   its classifier is not stuck on "refuse": a child exiting 139 yields
   `CRASHED … subtotal_rc=1`, and `/usr/bin/true` (exit 0, no refusal token)
   yields `UNEXPECTED_OK … subtotal_rc=1`.
3. **Deliberately breaking inputs.** ADV1000 alloc pins ids 1–4 (`SIZE_MAX`,
   count overflow, injected realloc failure, double free); containers' BREAKING
   family is 142 cases; memstr passes up to 63 bytes past declared capacity;
   json_log pins 16 breaker/boundary ids; net_http's F6 uses >8 MB URLs. These
   are inputs that would corrupt memory unchecked.
4. **Asymmetric plain control.** The same `hostile.c` compiled without the
   checked layer actually corrupts 13 canaries and aborts 23 times — proof the
   inputs are genuinely hostile, not vacuous.
5. **Watchdog positive control.** `re_malformed_guard.cp` includes a child that
   deliberately trips the watchdog, proving the hang detector is live.
6. **Violation trap disabled only for observation.** `cplus_runtime_set_violation_trap(0)`
   lets the harnesses print refusals and a summary instead of fail-stopping with
   exit 70; production default is unchanged.
7. **Determinism and digests.** Fixed seeds and byte-identical re-runs: ADV1000
   per-group SHA-256 records (e.g. alloc `bb3016c7…`, net_http `df3bb4da…`,
   thread_async `e1e08f0c…`), `cmp`-identical expanded runs 3/5 and two
   concurrent contention runs each `110 110 0 0`, screenshot capture exit 0 with
   all tokens found. The one honest digest exception (json_log timestamped sink
   log vs byte-identical canonical stream) is disclosed in `SECURITY_LOGS.md` §4.
8. **Upstream sanitizer crash evidence for CVE-like baselines.** Baseline
   `case_*.c` binaries genuinely crash under ASan/UBSan/TSan with named
   diagnostics (§5). See §8 for the toolchain caveat: these baseline runs used
   Homebrew clang 23 directly, not the project's C+ backend driver.

---

## 7. Reproducible entry points

```sh
sh tests/adversarial_1000/run_all.sh
timeout 120 sh tests/expanded_examples_adversarial/run_all.sh
sh tests/abi/stdc_safety/run.sh
./examples/screenshots/capture.sh
sh tests/security/hostile_inputs/run.sh                 # exit 2 = BLOCKED mode, 0 failures
sh tests/security/resource_exhaustion/run.sh --mode all
sh tests/security/stdlib/run.sh                          # aggregate discovery runner
sh tests/scenarios/cve_like/run_cve_like.sh             # safe-variant gate only
```

---

## 8. Sanitizer status (per `docs/security/verification-status.md` §4)

| Sanitizer | Status | Evidence in this ledger |
|---|---|---|
| ASan (`address`) | **UNSUPPORTED** by the selected C+ backend (`/usr/bin/clang`, Apple clang 16): driver compile-and-run probe rejects every attempt (build exit 1, no binary, no run) | No ASan pass is claimed for the checked layers here. CVE-like baseline ASan crashes were produced with **Homebrew clang 23.1.6 invoked directly** with `-fsanitize=address -isysroot MacOSX.sdk`, because Apple clang 16's ASan runtime aborts at startup even for `int main(void){return 0;}` (`CHECK failed: sanitizer_malloc_mac.inc:189`). This is a different toolchain path, not the project's supported backend. |
| Leak, Thread, Memory, CFI | **UNSUPPORTED** by the same backend probe | No pass claimed. CVE-like case 15's baseline TSan data race likewise used Homebrew clang 23 directly. |
| UBSan (`undefined`) | **Accepted by probe**, but the canonical `c+ --strict --sanitize=undefined` build is **BLOCKED at the current HEAD** by the external interface mismatch `cplus_runtime.c:129:23: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'` (local header ends at `CPLUS_EXIT_IO = 109`) | Where obtained: `stdc_safety` checked-UBSan build exit 0, identical `64 48 64 0`, **0** `runtime error` reports; `resource_exhaustion` used a **build-only** `-DCPLUS_EXIT_ARITHMETIC=110` shim (no source edit, instrumentation confirmed by `nm` `10` `__ubsan` symbols vs `0`); `hostile_inputs` used post-`--` Clang UBSan flags with `--sanitize=none` (parser TUs instrumented, C+ runtime not). |
| Integer | Accepted by probe; earlier matrix had 23 PASS / 4 genuine unsigned-overflow FAILs; **not re-executed** in the current collection | No fresh integer-sanitizer execution here. |
| `none` (`strict-none`) | Supported and used for the main campaigns | ADV1000, expanded examples, hostile_inputs, resource_exhaustion, security aggregate (all 15 invocations). |

`verification-status.md` §6 records the broader suite context as **BLOCKED /
NOT GREEN**: 5 of 6 requested general suites exited non-zero (external
CPLUS_EXIT_ARITHMETIC and missing `MacOSX27.sdk` blockers); only
`abi_compat: 69 passed, 0 failed` was clean. That status is about the compiler/
runtime integration, not a refutation of the bounded hostile-input results in
this ledger, but it must be reported alongside them.

---

## 9. Evidence, not proof — explicit statement

Every number above is a **runtime observation of defined refusal/result
contracts over fixed, deterministic inputs**. A passing result means only that
for those inputs, in that build, on that host, the tested entry point returned
its documented result and the local guards survived. It **does not** establish:

- absence of defects for other sizes, counts, interleavings, addresses,
  schedules, actor histories or configurations;
- pointer provenance, full ownership, lifetime, alias or use-after-free
  freedom — the checked layers enforce runtime constraints, not provenance
  (explicitly acknowledged by `alloc/NOTES.md`, `memstr/NOTES.md`, `SECURITY_LOGS.md`
  §6 and the CVE-like `NOTES_2.md`–`NOTES_5.md`);
- format-correctness of the whole C runtime, compiler or third-party libraries;
- a live-game, injected-DLL or Wine result — none was performed here;
- formal correctness or a safety guarantee. No "all green" or "100% memory
  safe" claim is valid from this ledger.

Known documented limits that bound the claims: ADV1000's allocator recent-free
cache is 1024 slots; generation-based stale-alias detection is conservative;
default archives are non-sanitized; json_log's 512-byte line buffer makes the
exact truncation boundary a defined-result range rather than an asserted byte;
net_http does not claim header-syntax validation (CRLF/control lines are copied
verbatim by design) and performs no DNS/external transfer; yyjson has no explicit
reader depth limit (`YYJSON_READER_DEPTH_LIMIT 0`) so nesting fixtures test
bounded termination, not a parser depth bound; no yyjson depth limit, no
`pthread_create` failure injection, no ASan.

---

## 10. Missing artifacts / PENDING

| Item | Status |
|---|---|
| ADV1000 ninth group and 9,000-scenario target | **PENDING** — only 8 group directories with `NOTES.md` exist on disk; no ninth row is invented. Re-running the dynamic-discovery runner after a ninth compliant group lands would include it automatically. |
| Raw ADV1000 aggregate stdout/stderr and per-group logs/final lines under `build/adversarial_1000/` | Gitignored (`build/`); the verbatim transcript is preserved in `RESULTS.md` and `SECURITY_LOGS.md`. Raw files are present locally but not checked in — treat on-disk raw artifacts as environment-dependent, not authoritative. |
| Checked-in raw cve_like gate transcript | **PENDING** — `tests/scenarios/cve_like/` contains only `run_cve_like.sh`, `NOTES_1..5.md` and `case_*.c`; the `10 passed, 0 failed` gate output is quoted verbatim in `NOTES_5.md`, not stored as a separate results file. |
| Canonical instrumented builds (`c+ --strict --sanitize=undefined` / ASan / TSan) | **PENDING / BLOCKED** — see §8; all UBSan evidence here uses documented workarounds and all ASan/TSan baseline evidence uses a different compiler. |
| Live-game / injected-DLL / Wine validation of hostile inputs | **PENDING / out of scope** for this ledger. |
| `tests/security/resource_exhaustion/summary.json` (top level) | Not present at that path; the authoritative machine-readable summary is the hidden `tests/security/resource_exhaustion/.reout/summary.json` (`PASS 16 / FAIL 0 / BUILD_FAILURE 0 / TIMEOUT 0 / SKIPPED 0 / BLOCKED 0`). Path clarification, not a missing result. |
| `tests/security/stdlib` aggregate JSON/table | Present at `build/security/security_summary.json` / `security_table.txt` (generated 2026-11-23T04:13:26Z, mode `strict-none` only, 15 invocations PASS). Gitignored build artifact. |

---

## 11. Authoritative sources

- `tests/adversarial_1000/RESULTS.md`, `tests/adversarial_1000/SECURITY_LOGS.md`, and the eight `<group>/NOTES.md`.
- `tests/expanded_examples_adversarial/RESULTS.md` (including `logs/final.out`, run history, harness fix).
- `tests/abi/stdc_safety/RESULTS.md`.
- `examples/screenshots/MANIFEST.md` and the six `examples/screenshots/captures/*.txt` transcripts.
- `tests/security/hostile_inputs/NOTES.md`; `tests/security/resource_exhaustion/NOTES.md`, `README.md`, `.reout/summary.json`; `tests/security/stdlib/README.md`, `FS_SECURITY_NOTES.md`, `THREAD_SECURITY_NOTES.md`; `build/security/security_summary.json`, `security_table.txt`.
- `tests/scenarios/cve_like/NOTES_1.md` … `NOTES_5.md`.
- `docs/security/verification-status.md` (external blockers and sanitizer status).

No source file, test, header, archive, or git index was modified to produce this
document.

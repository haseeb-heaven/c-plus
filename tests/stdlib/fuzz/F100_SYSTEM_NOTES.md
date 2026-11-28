# F-094 .. F-100 — deterministic fuzz configurations (fs / time / net / error / log)

## 1. Scope and strict ownership

All seven configurations are standalone `.cp` programs with their own `main`,
following the established `tests/stdlib/fuzz/fuzz_hp.cp` model: fixed-seed LCG,
bounded operation counts, an in-process `SIGALRM` watchdog and an external
`timeout 30s`. Only these files were created by this agent:

| ID | File | Area / coverage |
| --- | --- | --- |
| F-094 | `f100_fs_pathgen.cp` | fs path generation: PID-unique temp names, empty/NULL/embedded-NUL/over-long (`PATH_MAX+128`) paths, bad `XXXXXX` templates, too-small template buffers (size 0/5/6); cleanup proof via `/tmp` prefix count. |
| F-095 | `f100_fs_cleanup.cp` | fs lifecycle: repeated create/remove, rename, repeated mkdir, bounded mkdtemp cycles over PID-unique paths; independent shadow bitmap checked after every case; `/tmp` prefix + `/dev/fd` cleanup proof. |
| F-096 | `f100_time_iso8601.cp` | time formatting: 11 hard-coded fixed timestamps (epoch, sub-second truncation, leap day, 2038, 2100, 2262), buffer sizes 0..24/32/64, NULL buffer, 400 seeded timestamps checked against an independent `gmtime_r`/`strftime` oracle. |
| F-097 | `f100_net_peers.cp` | net peers: `listener(0)` on `INADDR_LOOPBACK` with a **forked** connector, AF_UNIX `socketpair` with a **forked** writer, invalid ports (-1, `INT_MIN`, 65536, 70000), zero-initialized/borrowed inert handles, `poll()` 2 s cap, `/dev/fd` leak check. |
| F-098 | `f100_net_partial.cp` | net partial reads: forked chunked writer over AF_UNIX, `connection_borrow` reads with seeded small capacity, `STDLIB_EOF` proof, 64 KiB `connection::write` short-write retry with a forked drainer, documented invalid-argument failures. |
| F-099 | `f100_error_obj.cp` | error objects: construction/set/set_category/clear, message truncation at 0/1/2/127/254/255/256/257/300/320 bytes, exit-code→category mapping, NULL free helpers, re-entrant callback guard, 600 seeded cycles with telemetry accounting. |
| F-100 | `f100_log_lines.cp` | log levels/parse (incl. case-insensitive + invalid + NULL), message lengths 0..600 around `LOG_LINE_SIZE=512`, fields 0..3, thresholds 0..6, 300 repeated writes; newline count in the owned log file must equal the number of non-filtered OK writes; borrowed sink drop safety; file cleanup proof. |

No header, build script, source or other test file was modified. Build artefacts
were kept under the harness temp directory, not the repository.

## 2. Determinism and termination

- Every config has a distinct fixed LCG seed (`0xF0941234` .. `0xF1001234`).
  No wall clock value, `rand()`, ephemeral port number or random `mkdtemp`
  suffix is folded into a digest. Log lines contain a live timestamp, so only
  return codes, categories and the newline count are verified there.
- Operation counts are bounded (500 / 600 / 400 / 8 rounds / 12 rounds / 600 /
  300). Every socket wait uses `poll()` with a 2 s cap; every process has a
  20–25 s `SIGALRM` watchdog that writes one line to stderr and `_exit(90)`;
  every build and run was additionally wrapped in `timeout 30s`.
- Each config prints a 16-hex-digit `<id>_digest` line and an `<id>: N cases ok`
  line. F-096 additionally prints one deterministic `SKIP_REASON` line (§5);
  F-100 prints one `f100_borrowed_sinks_alive` marker line.
- Repeated runs (3 per config in default mode, plus strict-none and UBSan)
  produced byte-identical digests for all seven configs.

## 3. Exact verification commands and results

Run from the repository root on `agent/modern-stdlib`; `build/c+` is the
project-built compiler. Temp dir:
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/f100-fuzz`.

For each `<file>` in the table in §1, the strict/no-sanitizer pass was run
**first**, then UBSan:

```sh
TMP=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/f100-fuzz
mkdir -p "$TMP"

# 1) strict, sanitizers off
./build/c+ build tests/stdlib/fuzz/<file>.cp --strict --sanitize=none \
  -o "$TMP/<file>_strict_none"
timeout 30s "$TMP/<file>_strict_none"

# 2) strict + UBSan
./build/c+ build tests/stdlib/fuzz/<file>.cp --strict --sanitize=undefined \
  -o "$TMP/<file>_ubsan"
timeout 30s "$TMP/<file>_ubsan"

# 3) default build (extra cross-mode digest check)
./build/c+ build tests/stdlib/fuzz/<file>.cp -o "$TMP/<file>_default"
timeout 30s "$TMP/<file>_default"

# 4) type check
./build/c+ check tests/stdlib/fuzz/<file>.cp
```

Results (build rc = 0, run rc = 0 for every row; no build/run reached the
30 s limit; no `INVARIANT` line was printed):

| Config | strict-none digest / status | UBSan digest / status | default digest | stderr (strict / UBSan) | `c+ check` |
| --- | --- | --- | --- | --- | --- |
| F-094 | `16ae28f1da12e2cb` / 500 cases ok | `16ae28f1da12e2cb` / 500 cases ok | `16ae28f1da12e2cb` | 0 B / 0 B | ok |
| F-095 | `4f4cc918727917a7` / 600 cases ok | `4f4cc918727917a7` / 600 cases ok | `4f4cc918727917a7` | 0 B / 0 B | ok |
| F-096 | `937c14b6820956de` / 400 cases ok | `937c14b6820956de` / 400 cases ok | `937c14b6820956de` | 0 B / 0 B | ok |
| F-097 | `ff9a9d038b8abb4b` / 8 rounds ok | `ff9a9d038b8abb4b` / 8 rounds ok | `ff9a9d038b8abb4b` | 0 B / 0 B | ok |
| F-098 | `0131ea0cc687ab7c` / 12 rounds ok | `0131ea0cc687ab7c` / 12 rounds ok | `0131ea0cc687ab7c` | 0 B / 0 B | ok |
| F-099 | `7ac6713b305844b3` / 600 cases ok | `7ac6713b305844b3` / 600 cases ok | `7ac6713b305844b3` | 0 B / 0 B | ok |
| F-100 | `51911674c454a3ce` / 300 cases ok | `51911674c454a3ce` / 300 cases ok | `51911674c454a3ce` | 37 B / 37 B (see §5) | ok |

Determinism re-check (default mode, 3 runs each) — all seven reported an
identical digest on all three runs. No timeout exit (124) occurred.

## 4. Expected stdout

```
f094_digest 16ae28f1da12e2cb
f094: 500 cases ok
f095_digest 4f4cc918727917a7
f095: 600 cases ok
SKIP_REASON f096_secs_gt_long_max unreachable on 64-bit time_t (ULLONG_MAX/1e9 < LONG_MAX)
f096_digest 937c14b6820956de
f096: 400 cases ok
f097_digest ff9a9d038b8abb4b
f097: 8 rounds ok
f098_digest 0131ea0cc687ab7c
f098: 12 rounds ok
f099_digest 7ac6713b305844b3
f099: 600 cases ok
f100_borrowed_sinks_alive
f100_digest 51911674c454a3ce
f100: 300 cases ok
```

## 5. SKIPs, limitations and expected non-digest output

| Item | Reason / handling |
| --- | --- |
| F-096 `secs_u > LONG_MAX` branch | Always SKIPped with `SKIP_REASON f096_secs_gt_long_max unreachable on 64-bit time_t (ULLONG_MAX/1e9 < LONG_MAX)`. On a 64-bit `time_t`, `ULLONG_MAX/1e9 < LONG_MAX`, so no `unsigned long long` nanosecond value can trigger it. A constant is folded into the digest so the oracle stays host-independent. |
| F-094/F-095 sandbox | Conditional SKIP if PID-unique `fs_mkdtemp` fails; not triggered here. |
| F-097 AF_UNIX / fd count | Conditional SKIP if `socketpair`/`fork` or `opendir(/dev/fd)` is unavailable; not triggered here. |
| F-098 partial / big-write / fd count | Conditional SKIP on the same capability failures; not triggered here. |
| F-100 file sink | Conditional SKIP if `log_open_file` cannot open the PID-unique log; not triggered here. |
| AddressSanitizer | Not verified: the selected C++ backend rejects `-fsanitize=address` (see `docs/stdlib/HP_FUZZ_NOTES.md` §7). Required strict/no-san and UBSan modes both pass. |
| F-100 stderr (37 B) | One line from the intentional `log_stderr(LOG_TRACE)` write proving a borrowed sink still works after `drop()`; its timestamp varies, so it is not part of any byte-exact oracle. All other configs produce empty stderr. |

## 6. Findings

- **No library defect found.** All shadow-model comparisons, error-category
  assertions, cleanup proofs, `/dev/fd` checks and exact ISO-8601 strings held
  in every mode. There was no crash, sanitizer report, hang or `INVARIANT`
  line in any of the 21 build+run verification invocations.
- **Harness defect found and fixed (F-098).** The first version folded the
  number of `connection::read()` calls into the digest. Because a forked child
  writes in small chunks, the kernel may coalesce or split arriving data, so
  five identical runs produced three different digests
  (`b59bf4773f5cfd80`, `ec8e3116379763ab`, `509ca74f8e8ea023`). Read-call
  count is now excluded (byte totals are still checked); five consecutive
  runs then all produced `0131ea0cc687ab7c`. Repro of the old behaviour: run
  the pre-fix binary five times and compare the `f098_digest` lines.
- Known, previously documented fs behaviour (failed `fs_mkdtemp` mutates the
  caller's template; see `docs/stdlib/DEEP_FS_TASK5_CONTRACT.md`) is handled by
  rebuilding every template before use, so no new bug is claimed here.

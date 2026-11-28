# extra_time_boundaries — time.hp boundary test (agent 2)

## Owned files

- `tests/stdlib/agent2/extra_time_boundaries.cp`
- `tests/stdlib/agent2/extra_time_boundaries_NOTES.md` (this file)

No header, existing test, or other agent file was modified. No git add/commit
was performed. The program writes no files and spawns no processes. All buffers
are stack-local. No wall-clock-dependent expectation is used beyond clock
non-decrease and deliberately generous sleep bounds.

## What the single program covers

| # | Required path | Tags |
|---|---------------|------|
| 1 | `time_now_ns` / `time_now_ms` / `time_monotonic_ns` / `time_monotonic_ms` positivity | `now_ns_positive`, `now_ms_positive`, `mono_ns_positive`, `mono_ms_positive` |
| 2 | All four clock helpers over 100 consecutive samples, never decreasing | `now_ns_100_nondec`, `now_ms_100_nondec`, `mono_ns_100_nondec`, `mono_ms_100_nondec` |
| 3 | `time_sleep_ms(0)` prompt; `time_sleep_ms(1)` ~1 ms | `sleep_zero_prompt`, `sleep_one_ms` |
| 4 | Exact ISO-8601 for 0, 1, 86400 (ns), 1e9 (ns), one full day, `UINT64_MAX` | `iso_epoch_0`, `iso_one_ns`, `iso_86400_ns`, `iso_one_sec_1e9`, `iso_one_day`, `iso_uint64_max` |
| 5 | NULL output buffer; NULL buffer + NULL error; zero size | `iso_null_buf`, `iso_null_buf_null_err`, `iso_zero_size` |
| 6 | Invalid/truncated sizes where the API allows them (1/4/20 bytes) | `iso_size_one`, `iso_size_four`, `iso_size_twenty` |
| 7 | Exact-fit 21-byte buffer, 20-char output, adjacent guards | `iso_exact21_guards` |
| 8 | Roomy buffer leaves byte 21 untouched | `iso_roomy_guard` |
| 9 | Unsigned wrapped-negative caller value | `iso_wrapped_negative` |
| 10 | Recovery / repeated conversion after a failure | `iso_recovery_after_failure`, `iso_repeated_valid` |

The `86400` case is passed literally as **86400 unix nanoseconds** (0.0864 ms,
truncated to 0 seconds -> epoch string). To remove any ambiguity in the phrase
"86400", a separate `iso_one_day` check passes `86400 * 1e9` ns (one full day)
and expects `1970-01-02T00:00:00Z`. Both are fixed constants.

## Exact commands (run from the repository root)

`ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
`TMPD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/extra_time_boundaries`

STRICT --sanitize=none (run FIRST):

```sh
timeout 30 "$ROOT/build/c+" build \
  "$ROOT/tests/stdlib/agent2/extra_time_boundaries.cp" \
  --strict --sanitize=none -o "$TMPD/etb_strict_none"
timeout 30 "$TMPD/etb_strict_none" > "$TMPD/strict_none.out" 2> "$TMPD/strict_none.err"
```

Default:

```sh
timeout 30 "$ROOT/build/c+" build \
  "$ROOT/tests/stdlib/agent2/extra_time_boundaries.cp" \
  -o "$TMPD/etb_default"
timeout 30 "$TMPD/etb_default" > "$TMPD/default.out" 2> "$TMPD/default.err"
```

UBSan:

```sh
timeout 30 "$ROOT/build/c+" build \
  "$ROOT/tests/stdlib/agent2/extra_time_boundaries.cp" \
  --strict --sanitize=undefined -o "$TMPD/etb_ubsan"
timeout 30 "$TMPD/etb_ubsan" > "$TMPD/ubsan.out" 2> "$TMPD/ubsan.err"
```

## Per-mode results

| Mode | Build rc | Run rc | Final stdout line | stderr | Verdict |
|------|----------|--------|-------------------|--------|---------|
| strict / sanitize=none | 0 | 0 | `EXTRA_TIME_BOUNDARIES 27 0` | empty | PASS |
| default | 0 | 0 | `EXTRA_TIME_BOUNDARIES 27 0` | empty | PASS |
| strict / sanitize=undefined (UBSan) | 0 | 0 | `EXTRA_TIME_BOUNDARIES 27 0` | empty | PASS |

All three stdout files are byte-identical (`diff` clean in both comparisons).
Each has 28 lines: 27 `<tag> <0|1>` check lines, all `1`, then the final
summary line. Exit code is 0 in every mode; `timeout 30` was never approached.

## Exact stdout (all three modes, byte-identical)

```
now_ns_positive 1
now_ms_positive 1
mono_ns_positive 1
mono_ms_positive 1
now_ns_100_nondec 1
now_ms_100_nondec 1
mono_ns_100_nondec 1
mono_ms_100_nondec 1
sleep_zero_prompt 1
sleep_one_ms 1
iso_epoch_0 1
iso_one_ns 1
iso_86400_ns 1
iso_one_sec_1e9 1
iso_one_day 1
iso_uint64_max 1
iso_null_buf 1
iso_zero_size 1
iso_null_buf_null_err 1
iso_size_one 1
iso_size_four 1
iso_size_twenty 1
iso_exact21_guards 1
iso_roomy_guard 1
iso_wrapped_negative 1
iso_recovery_after_failure 1
iso_repeated_valid 1
EXTRA_TIME_BOUNDARIES 27 0
```

## Observed / documented-but-surprising behavior (not defects)

1. **Unsigned-nanosecond wrap.** `time_format_iso8601` takes
   `unsigned long long unix_ns`; it truncates fractional seconds (no rounding)
   and formats the whole seconds. `ULLONG_MAX` ns (`18446744073709551615`)
   therefore yields the well-formed future date `2554-07-21T23:34:33Z`, and a
   negative caller value such as `(unsigned long long)(-86400000000000LL)`
   wraps to a large positive and also formats as a well-formed future date.
   These are **documented consequences** of the unsigned API, consistent with
   `tests/stdlib/deep_fs_time.cp:220-227` and `deep_fs_task5.cp:385-388,
   417-422`; the test records them as OK and does not force a failure.
2. **INVALID vs BOUNDS asymmetry.** `buf == NULL` and `size == 0` fail with
   category `STDLIB_CATEGORY_INVALID` / `CPLUS_EXIT_UNSUPPORTED`; truncated
   sizes 1..20 fail with `STDLIB_CATEGORY_BOUNDS` / `CPLUS_EXIT_BOUNDS`, and
   the header explicitly writes `buf[0] = '\0'` on those failures. `size == 0`
   leaves `buf[0]` untouched (the `'X'` sentinel survives); NULL buffer writes
   nothing. This asymmetry is deliberate (`stdlib/time.hp:60-82`).
3. **`err == NULL` is tolerated**, including on the failure path
   (`error_reset` / `error_fail_cat` are no-ops when the target is NULL).
4. **100-sample non-decrease for `time_now_*` (CLOCK_REALTIME).** Real time can
   be stepped by NTP; over this test's sub-second sampling window no step was
   observed, so non-decrease holds. This is the only wall-clock-dependent
   expectation in the suite; a live clock adjustment during the loop would be
   an environmental flake, not a library defect.

## Observed defects / findings

**No defect was reproduced.** No header change was needed and no minimal repro
of a real defect exists to report. The following paths remain UNTESTED in this
suite because they are not reachable/injectable in this environment (they are
documented, not claimed verified):

- `secs_u > LONG_MAX` range branch (`stdlib/time.hp:65-70`) — unreachable on a
  64-bit host (`ULLONG_MAX / 1e9 ~= 1.84e10 < LONG_MAX`).
- `gmtime_r == NULL` branch (`stdlib/time.hp:72-77`) — unreachable for the
  tested values.
- `clock_gettime` failure and negative `tv_sec`/`tv_nsec` from a clock — the
  guards are present but not injectable with real clocks.
- Non-`EINTR` `nanosleep` failures, multiple interrupts in one sleep, and
  `time_sleep_ms((unsigned)-1)` (~49-day sleep) — not invoked (the API is
  `void`, so there is no observable failure path).

Passing this suite is behavioural evidence only; it does not establish absence
of UB or memory-safety properties beyond the guard bytes asserted here.

## Runner note

The shared aggregate runner (`tests/stdlib/agent2/run_all.py`) auto-discovers
every `agent2/*.cp`, so if another agent invokes it while this file is present
it will emit `tests/stdlib/agent2/results/extra_time_boundaries.*` artifacts.
Those artifacts are produced by the shared runner, not by this test, and are
outside this task's strict file ownership; they were left untouched.

# FUZZ10K — format / conversion checked-API fuzz

Agent 4 of 10 (`agent/modern-stdlib`). Scope: `tests/fuzz10k/format/` only.
No source edits, no `git add`, no commits, no files outside this directory.

## Summary line (required form)

```
FUZZ10K format 10000 0 0 0 2187
```

Fields: `scenarios=10000 crashes=0 hangs=0 corruptions=0 refusals=2187`.
Exit status of `run.sh`: **0**. `crashes/hangs/corruptions` are all zero;
`refusals` are *expected* negative results (`%n`, unbounded `%s`/`%[`,
null dest/stream/input, scanf EOF, invalid numeric base) and are not failures.

## Files

| file | purpose |
| --- | --- |
| `fuzz_format.c` | C99 harness: fixed-seed PRNG, fixed safe format-token catalogue, guarded buffers, forked worker + watchdog, per-scenario log |
| `run.sh` | build with `/usr/bin/clang -std=c99`, link both archives, run under `timeout 600` with stdout to a sink, compute digest, print summary, enforce exit code |
| `.gitignore` | ignores generated `scenarios.log` and `digest.txt` |
| `scenarios.log` | generated: 10,000 scenario records + 1 `RESULT` line (3.6 MB, gitignored) |
| `digest.txt` | generated: SHA-256 of `scenarios.log` (gitignored) |
| `NOTES.md` | this file |

## Symbol verification (required by task)

```
$ nm -g --defined-only build/safe/libcplus_safecrt.a | grep -E ' T _cplus_(sprintf_s|snprintf_s|printf_s|fprintf_s|sscanf_s|fscanf_s|runtime_set_violation_trap|safe_log_set_level)$'
0000000000000918 T _cplus_fprintf_s
0000000000001094 T _cplus_fscanf_s
0000000000000614 T _cplus_printf_s
00000000000003b0 T _cplus_snprintf_s
00000000000016dc T _cplus_sprintf_s
0000000000001360 T _cplus_sscanf_s
0000000000000000 T _cplus_safe_log_set_level
000000000000005c T _cplus_runtime_set_violation_trap

$ nm -g --defined-only build/libc/libcplus_hardened.a | grep -E ' T _cplus_(strtol|strtoul|strtoimax)$'
0000000000000104 T _cplus_strtol
0000000000000150 T _cplus_strtoul
00000000000001d0 T _cplus_strtoimax
```

Both archives are linked; the `_checked` entry points are deliberately not
used because they reject any runtime-composed (non-literal) format string
before parsing it, which would prevent the format parser from being fuzzed.
The base `cplus_*_s` functions still perform the substantive checked behavior:
`%n` refusal, unbounded-`%s`/`%[` refusal in the scanf family, null
dest/stream/input refusal, and bounded `dmax` writes with NUL termination.

## Determinism / methodology

- PRNG: xorshift64*, `SEED = 0x46555A5A3130304B` (ASCII `"FUZZ100K"`,
  decimal `5068056299094159435`). No time, pid, or external input enters
  scenario generation.
- **10,000 distinct scenarios.** A 16,384-slot open-addressing hash set of
  FNV-1a fingerprints over `(api, format bytes, dmax, olen, base, flags,
  input)` rejects any repeated parameter tuple and resamples it. Independently
  verified from the log: `10000` scenario lines, `10000` unique lines.
- **Fixed safe format set.** Formats are composed from static token tables
  grouped by the exact C type of the vararg consumed (`int`, `unsigned`,
  `long long`, `unsigned long long`, `size_t`, `char*`, `int*` for printf;
  matching pointer groups for scanf). A scenario picks one group and emits
  1–6 type-matched tokens plus literal fillers; exactly six correctly typed
  varargs are passed (extra varargs are ignored by C variadic semantics).
  No PRNG byte is ever used as a format string. This is the same discipline
  as `tests/crt/fuzz/f100_fmt_common.h`.
- Random per-scenario parameters: API (9-way), dmax `0..512`, output/input
  length `0..512`, token count `1..6`, base `{-1,0,2,8,10,16,36,37}`, and
  null flags. Integer values include `INT_MIN`, `INT_MAX`, `UINT_MAX`,
  `LLONG_MIN`, `LLONG_MAX`, `ULLONG_MAX`, `SIZE_MAX` and overflowing numeric
  strings. `%n` is forced every 97th scenario plus random selection.
- Canaries: 16 bytes before/after a 520-byte destination; `dmax <= 512`, so
  every byte from `buf[dmax]` to `buf[519]` must remain canary (no unbounded
  write). NUL placement is checked at `buf[rc]` when `rc < dmax`, else at
  `buf[dmax-1]`. Guarded `%n` sinks must keep their `0x5A5A5A5A` sentinel.
  scanf string scratch is checked past index 39 (max width is `%32s`).
- Verdicts: `OK` (rc>=0, not truncated), `TRUNC` (rc>=dmax), `REFUSED` (rc<0).
- printf-family output goes to stdout, which `run.sh` redirects to a sink
  file (`$WORK/stdout_sink`); only the summary is extracted and re-printed.
- Crashes/hangs are isolated in a forked worker: signal handlers for
  SIGSEGV/SIGBUS/SIGILL/SIGABRT/SIGFPE write a `RESULT ... crashes=1` record
  and `_exit(90)`; `alarm(540)` writes `hangs=1` and `_exit(91)`; the outer
  `timeout 600` is the backstop. The parent never executes fuzz calls.

## Exact commands (run from repo root)

```
sh tests/fuzz10k/format/run.sh
```

`run.sh` internally executes:

```
/usr/bin/clang -std=c99 -O1 -g -Wall -Wextra \
  -Wno-format-nonliteral -Wno-format-security -Wno-unused-function \
  -Istdlib/safe -Ilibc/install-include -Ilibc \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/fuzz10k/format/fuzz_format.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o "$WORK/fuzz_format"

timeout 600 "$WORK/fuzz_format" tests/fuzz10k/format/scenarios.log \
  > "$WORK/stdout_sink" 2> "$WORK/stderr"

shasum -a 256 tests/fuzz10k/format/scenarios.log   # -> digest.txt
```

Observed build: clean (no warnings/errors). Harness `rc=0`. Runtime ~2.3 s
wall (well under the 600 s limit). The 1,135 stderr lines are the library's
expected `[CPLUS-SAFE] <api>: forbidden ...` violation notices from refused
scenarios (the runtime prints these unconditionally; violation trap was set
to 0 with `cplus_runtime_set_violation_trap(0)` so refusals are countable).

## Results (two independent real runs, identical)

```
$ sh tests/fuzz10k/format/run.sh
[fuzz10k/format] harness rc=0
[fuzz10k/format] stderr lines (expected SAFECRT violation notices): 1135
[fuzz10k/format] log lines: 10001
[fuzz10k/format] sha256=48fcdddf7e8592d224d4cf86a100753b997a0dcdc1dd1e85340584d3c06bb92b
FUZZ10K format 10000 0 0 0 2187
```

Run 2 produced byte-identical `scenarios.log` (`cmp` clean) and the same
SHA-256. `RESULT` line in the log:

```
RESULT scenarios=10000 crashes=0 hangs=0 corruptions=0 refusals=2187 signal=0
```

Distribution (from the log):

```
verdict=OK       7558
verdict=REFUSED  2187
verdict=TRUNC     255
checks=ok       10000
checks=CORRUPT      0
```

Per-API coverage (all >= 1,000 scenarios):

```
1077 fprintf_s   1119 fscanf_s   1067 printf_s   1149 snprintf_s
1076 sprintf_s   1121 sscanf_s   1136 strtoimax  1104 strtol
1151 strtoul
```

Representative log records (truncated to 220 chars here; full bytes in the log):

```
87|api=snprintf_s|grp=5|dmax=185|olen=473|base=8|flags=0|fmt=%.8s%-16.6s-%-12s%-12s|...
12|api=fprintf_s|grp=6|dmax=273|olen=179|base=37|flags=0|fmt=!a%n|...|verdict=REFUSED
11|api=sprintf_s|grp=0|dmax=439|olen=492|base=36|flags=1|fmt=.!%.6d|... (null dest)
```

## Evidence, not proof

This is **evidence, not proof** of correctness. 10,000 deterministic,
type-correct, in-process scenarios with canary/bounds assertions found no
crash, hang, out-of-bounds write, or canary corruption in the two archives at
the tested build flags. The format space is intentionally bounded to a fixed
catalogue of safe specifiers (composed randomly, never attacker-supplied raw
bytes) and to `dmax <= 512`; inputs outside that envelope — notably truly
attacker-controlled format strings, other locales, bigger buffers, different
optimization levels, ASan/UBSan builds, and live game/Wine execution — were
not exercised here. A clean run lowers risk for this envelope; it does not
establish absence of defects.

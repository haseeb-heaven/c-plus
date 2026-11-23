# ADV1000 — checked format & I/O adversarial scenarios (agent 1/10)

STRICT FILE OWNERSHIP: only files under `tests/adversarial_1000/fmt_io/`
were created or modified. No compiler/runtime/header edits, no `git add`/commit.

## 1. Symbol verification (`nm`, both archives)

Command (from repo root):

```sh
nm -g --defined-only build/safe/libcplus_safecrt.a | grep -E 'sprintf_s|snprintf_s|printf_s|fprintf_s|sscanf_s|fscanf_s|cplus_fopen_s|fread_s|fwrite_s|fclose_s' | sort -u
nm -g --defined-only build/libc/libcplus_hardened.a | grep -E 'sprintf_s|snprintf_s|printf_s|fprintf_s|sscanf_s|fscanf_s|cplus_fopen_s|fread_s|fwrite_s|fclose_s' | sort -u
```

Result: the checked API set lives entirely in `build/safe/libcplus_safecrt.a`:

```
_cplus_fopen_s            _cplus_fclose_s           _cplus_fread_s
_cplus_fwrite_s           _cplus_printf_s           _cplus_printf_s_checked
_cplus_fprintf_s          _cplus_fprintf_s_checked  _cplus_snprintf_s
_cplus_snprintf_s_checked _cplus_sprintf_s          _cplus_sprintf_s_checked
_cplus_sscanf_s           _cplus_sscanf_s_checked   _cplus_fscanf_s
_cplus_fscanf_s_checked   _cplus_vsnprintf_s        _cplus_vsprintf_s
```

`build/libc/libcplus_hardened.a` defines **0** checked-`_s` symbols (verified
separately: `nm ... | grep -c ...` returned `0`). Both archives are still linked
because the safecrt adapters reference the hardened libc support layer, matching
the repository's existing `tests/crt/fuzz/f100_fmt_run.sh` link line.

## 2. Harness design (`adv1000_fmt_io.c`)

- **Determinism**: fixed-seed Numerical Recipes LCG (`0xAD1000F1`); every one of
  the 1000 scenarios consumes exactly six draws (r0..r5), so output is
  byte-for-byte reproducible across runs.
- **Distinctness**: scenario IDs 1..1000 are unique; the 46-entry `(api, case)`
  catalogue is cycled (`(id-1) % 46`), giving 21-22 scenarios per case and
  42-154 per API, with PRNG-varied parameters (`dmax`/`outlen` 0..256,
  capacity 8..256, binary payload 0..4096 bytes).
- **Fixed safe format set**: 19 type-matched format strings (no `%n`). Random
  bytes NEVER become a format string. A separate 6-entry *forbidden* catalogue
  (`%n`, `%hhn`, `%lln`, `AB%n`, ...) is passed **only** to APIs that must refuse
  it before any conversion runs; with `cplus_runtime_set_violation_trap(0)` the
  SafeCRT returns `-1/EINVAL`. No real `%n` write is ever executed.
- **Canaries**: each scenario uses a 16-byte prefix + 4 KiB data region +
  16-byte postfix filled with `0x5A`. String/scan cases additionally verify
  the whole region beyond `dmax` (or beyond `width+1`) stays canary, and that
  output is NUL-terminated within `dmax` when `dmax > 0`. I/O one-over cases use
  a 16-byte data + 16-byte guard struct.
- **APIs covered**: `sprintf_s`, `snprintf_s`, `printf_s`, `fprintf_s`,
  `sscanf_s`, `fscanf_s`, `fopen_s`, `fread_s`, `fwrite_s`, `fclose_s`
  (base `cplus_*` adapters and the `*_checked` variants for one-over and
  non-literal refusals).
- **Required adversarial cases** (all present and observed):
  - oversized format output into small buffers → `TRUNC`, canary intact
  - null stream writes/reads → `REFUSED` (`fprintf_s`, `fwrite_s`, `fread_s`, `fscanf_s`)
  - read/write one-over → `CLAMPED` to `bmax/size`, guard intact
  - binary roundtrip payloads 0..4096 bytes → byte-exact, canary intact
  - double close → second `fclose_s` returns `EOF/EINVAL`, no UAF
  - buffer sizes 0..256, one-over `dmax`, null dest/input/format/mode
- **Temp files**: `TMPDIR` (else `/tmp`) + `getpid()` + scenario id; `tmpfile()`
  where a name is not required. No user paths. All artifacts in gitignored
  `results/` (local `.gitignore` added).
- **Output**: `S <id> <api> <case> <result> <canary_ok>` × 1000, then
  `ADV1000 fmt_io 1000 <refused> <canary_ok> <failures>`. Exit nonzero if any
  failure or any canary break.

## 3. Exact commands and results

Build + run (wrapper `run.sh`, executed as `sh tests/adversarial_1000/fmt_io/run.sh`
from the repo root). Core command:

```sh
/usr/bin/clang -O1 -g -std=c99 -Wall -Wextra \
    -Wno-format-nonliteral -Wno-format-security -Wno-unused-function \
    -Istdlib/safe -Ilibc/install-include -Ilibc \
    -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
    tests/adversarial_1000/fmt_io/adv1000_fmt_io.c \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
    -lm -pthread -o tests/adversarial_1000/fmt_io/results/adv1000_fmt_io
timeout 120 tests/adversarial_1000/fmt_io/results/adv1000_fmt_io \
    > tests/adversarial_1000/fmt_io/results/scenarios.log \
    2> tests/adversarial_1000/fmt_io/results/scenarios.err
```

Observed (clean build, no warnings):

```
harness exit = 0
scenario lines = 1000 ; summary lines = 1
ADV1000 fmt_io 1000 654 1000 0
digest: b366a11e13cb32f495e8387658ae591f88d492fc4f876f9ee0ae23190f0141b3  .../scenarios.log
```

Interpretation: 1000 scenarios, **654 REFUSED**, **1000 canary_ok**, **0 failures**.

Result-token distribution: `REFUSED=654, OK=183, TRUNC=79, CLAMPED=42, ZERO=42`.
Canary column: 1000 × `1`. Per-case counts are 21-22 (even coverage).

Example lines from `results/scenarios.log`:

```
S 1 snprintf_s TRUNC TRUNC 1
S 5 snprintf_s ONE_OVER REFUSED 1
S 18 fprintf_s NULL_STREAM REFUSED 1
S 38 fread_s READ_ONE_OVER CLAMPED 1
S 43 fwrite_s WRITE_ONE_OVER CLAMPED 1
S 44 fwrite_s BIN_ROUNDTRIP OK 1
S 45 fclose_s DOUBLE_CLOSE REFUSED 1
```

Determinism: two consecutive full runs produced the identical SHA-256 digest
(printed above) and identical summary.

Digest is stored in `results/scenarios.log.sha256`.

## 4. Optional UBSan evidence

Additional build (not part of the required command) with
`-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero`,
run under `timeout 120`:

- exit `0`, summary `ADV1000 fmt_io 1000 654 1000 0` (identical to normal run)
- `grep -ci 'runtime error:' results/scenarios_ubsan.err` → `0` (no UBSan report)
- stderr contains only expected `[CPLUS-SAFE] ...` refusal diagnostics from
  disabled-trap violation returns.

## 5. Evidence, not proof

These 1000 deterministic scenarios are **evidence**, not proof. They exercise
the named checked functions with fixed inputs and verify bounded results and
canary integrity under this toolchain/build. They do not constitute a formal
proof of memory safety, do not cover every possible input or interleaving
(including multi-threaded or live-game/Wine behavior), and a passing result must
not be read as a claim of live-game success. No live-game injection was performed.

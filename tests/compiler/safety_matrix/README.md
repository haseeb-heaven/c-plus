# Phase 5 safety matrix

A real, evidence-producing runner for the C+ compiler's safety behaviour.
It executes every case in `cases.json` under six configurations and writes
`summary.json` (full compiler/runtime stdout, stderr, exit codes, commands,
backend, sanitizer flags, pass/fail) and `summary.txt` (matrix and failures).

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/safety_matrix/run_safety_matrix.py
```

Exit status is 1 only when `FAIL > 0` or any check times out. `WARN_ONLY`,
`KNOWN_OPEN` and `STATIC_ONLY_BY_DESIGN` are non-failing but printed and
recorded explicitly in both summaries. There is no `|| true` and no suppressed
failure. Each subprocess has a hard timeout (`CPLUS_MATRIX_TIMEOUT`, default
45 s); a timeout is recorded as FAIL.

## Configurations

| id | command | backend |
|----|---------|---------|
| `DEFAULT_NONE` | `c+ check --default --sanitize=none` | Apple clang 16 (`/usr/bin/clang`) |
| `STRICT_NONE` | `c+ check --strict --sanitize=none` | Apple clang 16 |
| `DEFAULT_SAN` | `c+ build --default --sanitize=address,undefined ... && ./exe` | Homebrew clang + installed SDK |
| `STRICT_SAN` | `c+ build --strict --sanitize=address,undefined ... && ./exe` | Homebrew clang + installed SDK |
| `DEBUG_NONE` | `c+ build --debug --sanitize=none ... && ./exe` | Apple clang 16 |
| `RELEASE_NONE` | `c+ build --release --sanitize=none ... && ./exe` | Apple clang 16 |

The two static configs use `check` (pure static analysis). The four runtime
configs use `build` followed by execution of the produced binary. Sanitizer
configs set `CPLUS_CLANG` explicitly: the system Apple clang 16 driver fails
the compiler's own compile-and-run sanitizer probe on this host, while the
Homebrew driver works when the installed SDK is supplied with
`-- -isysroot $(xcrun --show-sdk-path)`. Override with `CPLUS_SAN_CLANG` /
`CPLUS_SYSROOT`; override the non-sanitizer backend with `CPLUS_NONE_CLANG`.

## Oracle contract

Each entry in `cases.json` carries `check_evidence`
(`reject_required` | `warning_ok` | `known_static_gap`), `runtime_evidence`
(`trap_required` | `sanitizer_only` | `known_gap`), optional
`known_static_gap` / `static_only_by_design` / `sanitizer_gap` flags, and a
`why` note citing `docs/LIMITATIONS.md`, `docs/ARITHMETIC_SAFETY.md`,
`docs/SAFE_MODE_SPEC.md` or `docs/compiler/adversarial-wave2.md`.

* **PASS** — unsafe case rejected with the expected diagnostic, or built
  program traps with a non-zero/negative exit **and** an expected trap message
  (`AddressSanitizer`, `runtime error:`, `C+ runtime error:`, ...). Safe control
  PASSes only on a clean build+run with no trap text.
* **WARN_ONLY** — `DEFAULT --sanitize=none` is a WARN-only mode by design
  (`docs/SAFE_MODE_SPEC.md`, `docs/ARITHMETIC_SAFETY.md` section 3). If the
  check is accepted but emits the exact expected C+ warning, the result is
  WARN_ONLY, not PASS and not FAIL; the exact stderr is recorded in
  `summary.json` under `warning_diagnostic` / `compiler_stderr`.
* **KNOWN_OPEN** — documented static gap (`case_A1_implicit_narrow.c`,
  `case_A1_float_to_int.c`; see `docs/compiler/adversarial-wave2.md`) or the
  toolchain gap where `--sanitize=address,undefined` has no implicit-truncation
  check (narrowing runs exit 44). Non-failing, but listed with actual output.
* **STATIC_ONLY_BY_DESIGN** — `arith_divzero_strict.c` (provable constant zero
  divisor) and `case_CTRL_direct_overflow.c` (volatile `INT_MAX` initializer is
  a conservative compile-time fact, as pinned by `arith_shift_negative` /
  `arith_shift_wide`): STRICT rejects, DEFAULT warns, and DEBUG/RELEASE
  deliberately emit no native guard. Runtime-variable division is covered
  separately by `div_zero_runtime` with `runtime_evidence: trap_required` (rc 110).
* **FAIL** — missing required rejection/trap, wrong diagnostic, safe control
  misbehaving, or **timeout**. A clean compile of an unsafe program alone is
  never proof.

## Cases

23 unsafe cases (constant/dynamic/heap OOB, UAF, double-free, null deref,
signed overflow, constant and runtime-variable div-zero, invalid shift,
narrowing, float-to-int, uninitialized, array decay through a call, pointer
arithmetic, function-pointer free, struct field alias, recursion OOB,
while-loop OOB, phi-index OOB, use-after-move, managed escape,
double-release/cleanup) and 5 safe controls (bounds, ownership transfer, null
guard, loop bounds, cleanup drop). Some
reproducers are reused from `tests/compiler/interprocedural_safety/cases`,
`tests/compiler/adversarial_campaign` and `tests/compile_fail`; new ones live
in `cases/`.

# Results — bounded CLI framework (`stdlib/cli.hp`)

LIB agent 5/10, branch `agent/modern-stdlib`. All commands were run from the
repository root `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` with
`./build/c+` (C+ driver, Apple clang C backend). Nothing here is inferred from a
build alone: the harness was built **and executed**, and its stdout/stderr were
diffed byte-for-byte against a golden transcript embedded in `run.sh`.

## Headline

**27 checks · 27 passed · 0 failed. Exact stdout diff: PASS. Empty stderr: PASS.**

## Verified commands

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/stdlib/cli/run.sh          # rc=0
```

Exact runner stdout of the final verification run:

```text
+ ./build/c+ build /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/stdlib/cli/test_cli.cp --default --sanitize=none -I. -o /var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T//cli_test -- build/libc/libcplus_hardened.a
BUILD cli rc=0
+ /var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T//cli_test
PASS cli exact-output 27/27 checks
CLI SUITE PASS
```

The build uses the required invocation exactly:
`--default --sanitize=none -I. -- build/libc/libcplus_hardened.a`.

Final line of the harness binary: `CLI_TEST 27 27 0` (exit 0).

## Deterministic output (exact)

The exact stdout compared by `run.sh` is reproduced in full in the
`GOLDEN` heredoc of `run.sh`. It covers:

- 27 `CHECK <name> PASS` lines in registration order.
- The parsed descriptor block:
  `PARSE command=add id=1 found=1 options=2 args=2`, two `OPT` lines and two
  `ARG` lines — proving option/argument bounds and byte-exact token storage.
- The `HELP_BEGIN` / `HELP_END` markers around the exact `cli_help()` text:

```text
Commands:
  add - add two numbers (id=1)
  greet - greet a name (id=2)
Options:
  --count=value
  --verbose
```

## Coverage

| Area | Checks | Result |
|---|---|---|
| Construction (`cli_init`, `cli_add_command`, `cli_add_option`) | `init`, `add_cmd`, `add_opt` | PASS |
| Refusal — duplicate command/option | `dup_cmd`, `dup_opt` | PASS |
| Refusal — oversized name/help/token | `long_name`, `long_help`, `oversized_token` | PASS |
| Refusal — null pointers (12 call sites) | `null_args`, `null_token` | PASS |
| Refusal — uninitialised registry (magic check) | `uninit` | PASS |
| Refusal — registry capacity (16 commands / 8 options) | `registry_full` | PASS |
| Refusal — malformed option name / bare dash / flag-with-value / value-without-value | `bad_option_name`, `bare_dash`, `flag_value`, `value_missing` | PASS |
| Refusal — unknown option / unknown command / no command | `unknown_option`, `unknown_command`, `no_command` | PASS |
| Refusal — too many positional args (17 > 16) | `too_many_args` | PASS |
| Refusal — negative argc | `negative_argc` | PASS |
| Bounded parse + dispatch by id | `parse_ok`, `dispatch`, `dispatch_refusal` | PASS |
| Deterministic help + output bounds | `help_exact`, `help_bounds` | PASS |
| Stable error strings (`cli_error`) | `error_strings` | PASS |

## Compiler notes

`--default` is "report proven errors, warn if unverified", so the build emits
expected *warnings* (not errors) for the deliberate null-argument refusals and
for raw pointer extents in the library; the build rc is 0 and no runtime
safety check fires. clang also reports `build/libc/libcplus_hardened.a:
'linker' input unused` because this header needs no hardened-libc symbol; the
archive is still forwarded exactly as required by the task.

## File ownership

Only the assigned paths were created/modified:

- `stdlib/cli.hp` — header-only, `static inline`, plain C surface, no ABI change.
- `tests/stdlib/cli/test_cli.cp` — deterministic harness.
- `tests/stdlib/cli/run.sh` — bounded runner with exact-output golden.
- `tests/stdlib/cli/RESULTS.md` — this file.

No other files were touched; no `git add` / `git commit` was performed.

## Limitations

- Help rendering is fixed-format ASCII with registration order preserved; no
  wrapping, localization, or per-command option help is attempted.
- `cli_run` dispatches through a caller-supplied handler; the framework does
  not own or invoke application callbacks beyond that single call.
- Build/run verification only; no live-game or cross-platform claim is made.

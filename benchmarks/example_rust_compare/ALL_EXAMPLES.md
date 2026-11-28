# All 49 examples — C+ vs safe-Rust comparison index

This file enumerates every example discovered by `find examples demos` (source
files with a `.c` or `.cp` extension; `.h`, `.stdin`, `README.md` and runner
scripts are excluded), maps it to its comparison group and Rust source file,
and records the verdict from the aggregate runner. There are exactly **49**
examples: demos A–P (16), `examples/crt` (26), `examples/apps` (2),
`examples/system` (1), `examples/games` (4).

All paths below are relative to the repository root unless stated otherwise;
the `Rust file` column is relative to `benchmarks/example_rust_compare/`.

Result legend:

- **MATCH** — observable behavior parity for the tested inputs only.
- **DIFFERENT** — stdout, stderr or exit status differed for the tested inputs.
- **BLOCKED** — the prescribed build could not produce a comparable binary,
  or the example cannot run in this environment.
- The final aggregate run recorded in `RESULTS.md` has **0 PENDING** rows:
  43 MATCH, 5 DIFFERENT, 1 BLOCKED across all 49 examples.

## Index (49 examples)

| # | Example (source) | Group | Rust file | Result |
|---:|---|---|---|---|
| 1 | `demos/A_hello.c` | `demos_a_d` | `demos_a_d/A_hello.rs` | MATCH |
| 2 | `demos/B_bounds.c` | `demos_a_d` | `demos_a_d/B_bounds.rs` | DIFFERENT |
| 3 | `demos/C_resource.c` | `demos_a_d` | `demos_a_d/C_resource.rs` | MATCH |
| 4 | `demos/D_class.c` | `demos_a_d` | `demos_a_d/D_class.rs` | MATCH |
| 5 | `demos/E_networking.c` | `demos_e_h` | `demos_e_h/E_networking.rs` | MATCH |
| 6 | `demos/F_concurrency.c` | `demos_e_h` | `demos_e_h/F_concurrency.rs` | MATCH |
| 7 | `demos/G_borrow.c` | `demos_e_h` | `demos_e_h/G_borrow.rs` | MATCH |
| 8 | `demos/H_allocation_borrow.c` | `demos_e_h` | `demos_e_h/H_allocation_borrow.rs` | MATCH |
| 9 | `demos/I_raii_safe.c` | `demos_i_l` | `demos_i_l/I_raii_safe.rs` | MATCH |
| 10 | `demos/J_modern_io.c` | `demos_i_l` | `demos_i_l/J_modern_io.rs` | MATCH |
| 11 | `demos/K_state_class.c` | `demos_i_l` | `demos_i_l/K_state_class.rs` | BLOCKED |
| 12 | `demos/L_class_lifecycle.c` | `demos_i_l` | `demos_i_l/L_class_lifecycle.rs` | MATCH |
| 13 | `demos/M_default_args.c` | `demos_m_p` | `demos_m_p/M_default_args.rs` | MATCH |
| 14 | `demos/N_range_for.c` | `demos_m_p` | `demos_m_p/N_range_for.rs` | MATCH |
| 15 | `demos/O_extensions.cp` | `demos_m_p` | `demos_m_p/O_extensions.rs` | MATCH |
| 16 | `demos/P_std_library.cp` | `demos_m_p` | `demos_m_p/P_std_library.rs` | MATCH |
| 17 | `examples/crt/calloc_s.c` | `crt_memory` | `crt_memory/calloc_s.rs` | MATCH |
| 18 | `examples/crt/free_s.c` | `crt_memory` | `crt_memory/free_s.rs` | MATCH |
| 19 | `examples/crt/malloc_s.c` | `crt_memory` | `crt_memory/malloc_s.rs` | MATCH |
| 20 | `examples/crt/realloc_s.c` | `crt_memory` | `crt_memory/realloc_s.rs` | MATCH |
| 21 | `examples/crt/memcmp_s.c` | `crt_memory` | `crt_memory/memcmp_s.rs` | MATCH |
| 22 | `examples/crt/memcpy_s.c` | `crt_memory` | `crt_memory/memcpy_s.rs` | MATCH |
| 23 | `examples/crt/memmove_s.c` | `crt_memory` | `crt_memory/memmove_s.rs` | MATCH |
| 24 | `examples/crt/memset_s.c` | `crt_memory` | `crt_memory/memset_s.rs` | MATCH |
| 25 | `examples/crt/memzero_s.c` | `crt_memory` | `crt_memory/memzero_s.rs` | MATCH |
| 26 | `examples/crt/strcat_s.c` | `crt_string` | `crt_string/strcat_s.rs` | DIFFERENT |
| 27 | `examples/crt/strcmp_s.c` | `crt_string` | `crt_string/strcmp_s.rs` | DIFFERENT |
| 28 | `examples/crt/strcpy_s.c` | `crt_string` | `crt_string/strcpy_s.rs` | DIFFERENT |
| 29 | `examples/crt/strncat_s.c` | `crt_string` | `crt_string/strncat_s.rs` | MATCH |
| 30 | `examples/crt/strncpy_s.c` | `crt_string` | `crt_string/strncpy_s.rs` | MATCH |
| 31 | `examples/crt/strnlen_s.c` | `crt_string` | `crt_string/strnlen_s.rs` | DIFFERENT |
| 32 | `examples/crt/fclose_s.c` | `crt_io` | `crt_io/fclose_s.rs` | MATCH |
| 33 | `examples/crt/fopen_s.c` | `crt_io` | `crt_io/fopen_s.rs` | MATCH |
| 34 | `examples/crt/fprintf_s.c` | `crt_io` | `crt_io/fprintf_s.rs` | MATCH |
| 35 | `examples/crt/fread_s.c` | `crt_io` | `crt_io/fread_s.rs` | MATCH |
| 36 | `examples/crt/fscanf_s.c` | `crt_io` | `crt_io/fscanf_s.rs` | MATCH |
| 37 | `examples/crt/fwrite_s.c` | `crt_io` | `crt_io/fwrite_s.rs` | MATCH |
| 38 | `examples/crt/scanf_s.c` | `crt_io` | `crt_io/scanf_s.rs` | MATCH |
| 39 | `examples/crt/printf_s.c` | `crt_format` | `crt_format/printf_s.rs` | MATCH |
| 40 | `examples/crt/snprintf_s.c` | `crt_format` | `crt_format/snprintf_s.rs` | MATCH |
| 41 | `examples/crt/sprintf_s.c` | `crt_format` | `crt_format/sprintf_s.rs` | MATCH |
| 42 | `examples/crt/vsnprintf_s.c` | `crt_format` | `crt_format/vsnprintf_s.rs` | MATCH |
| 43 | `examples/apps/cgame.c` | `apps_system` | `apps_system/cgame.rs` | MATCH |
| 44 | `examples/apps/student.cp` | `apps_system` | `apps_system/student.rs` | MATCH |
| 45 | `examples/system/macpulse.cp` | `apps_system` | `apps_system/macpulse.rs` | MATCH (non-interactive tty-guard path only) |
| 46 | `examples/games/2048.c` | `games` | `games/2048.rs` | MATCH (deterministic core only) |
| 47 | `examples/games/snake.c` | `games` | `games/snake.rs` | MATCH (deterministic core only) |
| 48 | `examples/games/tetris.c` | `games` | `games/tetris.rs` | MATCH (deterministic core only) |
| 49 | `examples/games/tiles.c` | `games` | `games/tiles.rs` | MATCH (deterministic core only) |

## Reproduction and verification

All 49 rows were produced by the final aggregate run; there are no PENDING
entries. Re-run everything with this exact command (the runner discovers every
`<group>/compare.sh` automatically and refreshes every row):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/run_all.sh
```

To reproduce a single group with the same restricted environment the aggregate
runner uses, run its `compare.sh` from the repository root:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
PATH=/usr/bin:/bin:/usr/sbin:/sbin:"$(dirname "$(command -v rustc)")" \
CPLUS_CLANG=/usr/bin/clang LC_ALL=C \
  timeout 300 sh benchmarks/example_rust_compare/apps_system/compare.sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:"$(dirname "$(command -v rustc)")" \
CPLUS_CLANG=/usr/bin/clang LC_ALL=C \
  timeout 300 sh benchmarks/example_rust_compare/games/compare.sh
```

Each group runner prints one `EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED
<reason>` line per example. The exact per-example build/run commands for each
group are documented in that group's `RESULTS.md` and implemented in its
`compare.sh`; the aggregate runner does not rebuild examples itself.

## Explicit non-equivalence statement

**MATCH means observable behavior parity for the tested inputs only — it is
not a claim of Rust equivalence and not a 100% safety claim.**

- A MATCH verdict means that, for the specific deterministic inputs exercised,
  the two executables produced identical stdout, identical stderr and the same
  exit status. It says nothing about untested inputs or longer executions.
- C+ relies on compiler analysis plus **runtime** checks (checked index,
  allocation/RAII helpers, strict-mode fail-closed rejection); Rust enforces
  bounds, ownership, borrowing and null-representability with **compile-time**
  rules plus a small set of language-runtime checks. The mechanisms, failure
  modes and diagnostics are different even when the bytes printed are the same.
- SAFE-Rust ports here use `Result`/`Option`, `Vec`/`String`, `Drop` and
  `copy_within` rather than raw pointers and C runtime violation handlers.
  Several DIFFERENT verdicts exist solely because the C build links safeclib's
  global `ignore_handler_s`, which writes a constraint-handler line to stdout;
  safe Rust has no equivalent global side channel.
- No claim is made that any example is memory-safe, that the two languages give
  equivalent safety guarantees, or that passing these comparisons proves
  Rust equivalence. K_state_class is BLOCKED precisely because C+ strict mode
  rejects a construct that Rust accepts, which is direct evidence that the
  two toolchains are not interchangeable.
- Aggregate results are captured in `RESULTS.md`; per-group evidence and
  verbatim harness output live under each group's `RESULTS.md` and
  `build/example_rust_compare/<group>.log`.

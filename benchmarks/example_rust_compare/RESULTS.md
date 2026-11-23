# 49 example-vs-Rust comparisons — aggregate results

Recorded from a real run of `sh benchmarks/example_rust_compare/run_all.sh` on
2026-11-23 (branch `agent/modern-stdlib`). All 10 group runners were present
and every one of the 49 examples produced a verdict. No row below is
synthesized: each is the `EXAMPLE_COMPARE <name> <result> <reason>` line
emitted by the group's own `compare.sh` after building and running both
binaries. Per-group raw artifacts live under each group's own directories;
the aggregate's per-group logs are under `build/example_rust_compare/`.

## Toolchains

```
rustc 1.100.0 (0ef1bc386 2026-11-09)
Apple clang version 16.0.0 (clang-1600.0.26.6)   Target: arm64-apple-darwin27.2.0   (/usr/bin/clang)
C+ 0.1.0 (build/c+, C backend = /usr/bin/clang via CPLUS_CLANG)
GNU-style /usr/bin/timeout (30 s in group runners, 300 s hard bound in the aggregate runner)
Host: Darwin 27.2.0, arm64
```

## Reproduction

From the repository root (the runner pins its own environment; no Python, no
`|| true`, and no source edits):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/run_all.sh
```

The aggregate runner dynamically discovers every `<group>/compare.sh`, then
runs each from the repository root as:

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:"$(dirname "$(command -v rustc)")" \
CPLUS_CLANG=/usr/bin/clang LC_ALL=C \
  timeout -k 10 300 <group>/compare.sh
```

For a single group, run its `compare.sh` directly, e.g.:

```sh
sh benchmarks/example_rust_compare/demos_a_d/compare.sh
```

Each group's `RESULTS.md` documents its exact per-example build and run
commands. The `LANGUAGE` column in the table below is derived from the
original source extension: `.c` → `C/Rust`, `.cp` → `C+/Rust`.

## Exact final table (verbatim stdout of the aggregate run)

```
example-rust-compare aggregate: 10 group(s), timeout=300s, PATH=/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.cargo/bin, CPLUS_CLANG=/usr/bin/clang

EXAMPLE                  LANGUAGE   RESULT     REASON
-------                  --------   ------     ------
cgame                    C/Rust     MATCH      8/8 deterministic menu/quiz/sandbox paths byte-identical (stdout, stderr, exit); deeper level content not modeled
student                  C+/Rust    MATCH      2/2 deterministic CRUD+persistence paths byte-identical (stdout, stderr, exit, students.db)
macpulse                 C+/Rust    MATCH      non-interactive tty-guard path byte-identical (stderr + exit 1) via minimally patched copy; live interactive sampling is timing/hardware-dependent and BLOCKED for byte comparison (Rust --model is deterministic)
printf_s                 C/Rust     MATCH      stdout identical, exit=0
snprintf_s               C/Rust     MATCH      stdout identical, exit=0
sprintf_s                C/Rust     MATCH      stdout identical, exit=0
vsnprintf_s              C/Rust     MATCH      stdout identical, exit=0
fopen_s                  C/Rust     MATCH      stdout/exit (exit=0, stdout=[fopen_s: ok])
fclose_s                 C/Rust     MATCH      stdout/exit (exit=0, stdout=[fclose_s: ok])
fread_s                  C/Rust     MATCH      stdout/exit (exit=0, stdout=[fread_s: ok])
fwrite_s                 C/Rust     MATCH      stdout/exit (exit=0, stdout=[fwrite_s: ok])
fprintf_s                C/Rust     MATCH      stdout/exit (exit=0, stdout=[fprintf_s: ok])
fscanf_s                 C/Rust     MATCH      stdout/exit (exit=0, stdout=[fscanf_s: ok])
scanf_s                  C/Rust     MATCH      stdout/exit (exit=0, stdout=[scanf_s: ok])
malloc_s                 C/Rust     MATCH      stdout=13B exit=0; alloc failure/double-free via Option/Result
calloc_s                 C/Rust     MATCH      stdout=13B exit=0; alloc failure/double-free via Option/Result
realloc_s                C/Rust     MATCH      stdout=14B exit=0; alloc failure/double-free via Option/Result
free_s                   C/Rust     MATCH      stdout=11B exit=0; alloc failure/double-free via Option/Result
memcpy_s                 C/Rust     MATCH      stdout=73B exit=0; stdout incl. mirrored safeclib 406 handler line; refusal = Err(NoSpace)
memmove_s                C/Rust     MATCH      stdout=14B exit=0; overlap via copy_within; aliasing modelled as slice offsets
memset_s                 C/Rust     MATCH      stdout=71B exit=0; stdout incl. mirrored safeclib 406 handler line; refusal = Err(NoSpace)
memzero_s                C/Rust     MATCH      stdout=14B exit=0; zeroing via fill(0); zero-length = Err
memcmp_s                 C/Rust     MATCH      stdout=13B exit=0; ordering via return i32 (C uses diff out-param)
strcpy_s                 C/Rust     DIFFERENT  stdout differs, stderr identical, exit 0 vs 0 (C-only safeclib violation-handler line on stdout) (see out/)
strncpy_s                C/Rust     MATCH      stdout+stderr+exit identical (exit 0)
strcat_s                 C/Rust     DIFFERENT  stdout differs, stderr identical, exit 0 vs 0 (C-only safeclib violation-handler line on stdout) (see out/)
strncat_s                C/Rust     MATCH      stdout+stderr+exit identical (exit 0)
strcmp_s                 C/Rust     DIFFERENT  stdout differs, stderr identical, exit 0 vs 0 (C-only safeclib violation-handler line on stdout) (see out/)
strnlen_s                C/Rust     DIFFERENT  stdout differs, stderr identical, exit 0 vs 0 (C-only safeclib violation-handler line on stdout) (see out/)
A_hello                  C/Rust     MATCH      stdout+stderr+exit identical (exit 0)
B_bounds                 C/Rust     DIFFERENT  stdout identical, stderr differs, exit 134 vs 101 (see out/)
C_resource               C/Rust     MATCH      stdout+stderr+exit identical (exit 0)
D_class                  C/Rust     MATCH      stdout+stderr+exit identical (exit 0)
E_networking             C/Rust     MATCH      normalized ephemeral port + client/server line order; same 4 messages, CRs and exit; raw stdout differs only by those two inherent nondeterminism sources
F_concurrency            C/Rust     MATCH      exact stdout + exit; 3 threads x 50,000 locked increments, counter=150000
G_borrow                 C/Rust     MATCH      exact stdout + exit; static Rust borrow/Drop vs C+ runtime cleanup metadata
H_allocation_borrow      C/Rust     MATCH      exact stdout + exit; Vec length/Drop vs C+ managed-allocation length metadata
I_raii_safe              C/Rust     MATCH      stdout byte-identical, exit=0 (both)
J_modern_io              C/Rust     MATCH      stdout byte-identical, exit=0 (both)
K_state_class            C/Rust     BLOCKED    C+ strict build failed (rc=1): /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/demos/K_state_class.c:17:30: error: [CPLUS-ARITH] division by zero cannot be excluded
L_class_lifecycle        C/Rust     MATCH      stdout byte-identical, exit=0 (both)
M_default_args           C/Rust     MATCH      stdout bytes and exit 0 identical with empty stdin
N_range_for              C/Rust     MATCH      stdout bytes and exit 0 identical with empty stdin
O_extensions             C+/Rust    MATCH      stdout bytes and exit 0 identical with empty stdin
P_std_library            C+/Rust    MATCH      stdout bytes and exit 0 identical with empty stdin
2048                     C/Rust     MATCH      deterministic-core seed=12345; interactive UI excluded
snake                    C/Rust     MATCH      deterministic-core seed=12345; interactive UI excluded
tetris                   C/Rust     MATCH      deterministic-core seed=12345; interactive UI excluded
tiles                    C/Rust     MATCH      deterministic-core seed=12345; interactive UI excluded

example-compare-agg: groups=10 examples=49 match=43 different=5 blocked=1 hard_fail=1
example-compare-agg: expected 49 examples; logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/example_rust_compare/<group>.log
```

The aggregate runner also printed this diagnostic to stderr (the BLOCKED
build failure is reported, not hidden):

```
example-compare-agg: demos_i_l/K_state_class: build failure: C+ strict build failed (rc=1): /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/demos/K_state_class.c:17:30: error: [CPLUS-ARITH] division by zero cannot be excluded
```

Aggregate exit status: **1** — expected and intentional. The contract is
"nonzero only on a real DIFFERENT or a build failure"; there are 5 real
DIFFERENT results and 1 build-failure BLOCKED, so a zero exit would be a false
pass. The runner continued through every group and produced all 49 rows.

## Totals (out of 49)

| Result | Count |
|---|---:|
| MATCH | **43** |
| DIFFERENT | **5** |
| BLOCKED | **1** |
| Total | **49** |

Per-group breakdown:

| Group | Examples | MATCH | DIFFERENT | BLOCKED |
|---|---:|---:|---:|---:|
| `apps_system` | 3 | 3 | 0 | 0 |
| `crt_format` | 4 | 4 | 0 | 0 |
| `crt_io` | 7 | 7 | 0 | 0 |
| `crt_memory` | 9 | 9 | 0 | 0 |
| `crt_string` | 6 | 2 | 4 | 0 |
| `demos_a_d` | 4 | 3 | 1 | 0 |
| `demos_e_h` | 4 | 4 | 0 | 0 |
| `demos_i_l` | 4 | 3 | 0 | 1 |
| `demos_m_p` | 4 | 4 | 0 | 0 |
| `games` | 4 | 4 | 0 | 0 |
| **Total** | **49** | **43** | **5** | **1** |

Notes on the non-MATCH rows:

- The four `crt_string` DIFFERENT verdicts (`strcpy_s`, `strcat_s`, `strcmp_s`,
  `strnlen_s`) differ **only** by safeclib's process-global
  `ignore_handler_s` line printed to stdout by the C binary; safe Rust models
  the same refusals with `Result::Err` / `Option::None` and prints nothing.
  That group's supplementary normalized comparison (which removes that one
  C-only line) is `MATCH=6 DIFFERENT=0`.
- `B_bounds` DIFFERENT: both stop before the unreachable line and print the
  same stdout, but the C+ checked-index helper aborts (stderr diagnostic,
  exit 134) while Rust panics (own diagnostic, exit 101).
- `K_state_class` BLOCKED: C+ strict mode rejects the divide-by-zero guard at
  compile time (`[CPLUS-ARITH]`), so no strict C+ binary exists to compare;
  the Rust source builds and runs.
- `macpulse` is a MATCH only for the non-interactive tty-guard path using a
  minimally patched copy; live interactive sampling is timing/hardware
  dependent and was not byte-compared. The four games are MATCH only for
  their deterministic cores (seed 12345); interactive UI was excluded.
- E_networking normalizes inherently nondeterministic ephemeral-port and
  client/server line ordering; all messages, CRs and exit status match.

## Explicit non-equivalence statement

**MATCH means observable behavior parity for the tested inputs only — it is
not a Rust-equivalence claim and not a 100%-safety claim.**

- A MATCH verdict means only that the two executables produced identical
  stdout, stderr and exit status for the specific deterministic inputs
  exercised. It makes no claim about untested inputs, longer runs, hardware
  state, or concurrency schedules outside the tested case.
- C+ enforces behavior through compiler analysis plus **runtime** checks
  (checked index, managed allocation and RAII helpers, strict fail-closed
  rejection); Rust enforces bounds, ownership, exclusive borrowing and
  null-representability through **compile-time** rules with a small set of
  language-runtime checks. The mechanisms, diagnostics and failure modes are
  different even when printed bytes coincide.
- The safe-Rust ports use `Result`/`Option`, `Vec`/`String`, `Drop`, slices and
  `copy_within` instead of raw pointers or C runtime violation handlers.
  Nothing here demonstrates or proves memory safety of either language, and
  passing these comparisons does not establish equivalent safety guarantees.
- K_state_class is direct counter-evidence to any equivalence claim: C+ strict
  mode rejects a construct that Rust accepts and runs. The 5 DIFFERENT rows
  are genuine observable divergences, not harness errors.
- No claim of Rust equivalence, no claim of 100% safety, and no extrapolation
  from these 49 small programs to either language as a whole is made.

Index of every example, its group, its Rust file and its verdict:
`benchmarks/example_rust_compare/ALL_EXAMPLES.md`.

# App/system examples → safe-Rust behavioral comparison

Owner: `apps_system` subagent (branch `agent/modern-stdlib`).
All files produced by this task live under
`benchmarks/example_rust_compare/apps_system/`. No file in `examples/`,
`src/`, or anywhere else in the repository was modified.

Run everything with:

```sh
cd benchmarks/example_rust_compare/apps_system
./compare.sh | tee out/compare.run.log
```

## Exact toolchains

| tool | version / path |
| --- | --- |
| C original compiler | `/usr/bin/clang` → `Apple clang version 16.0.0 (clang-1600.0.26.6)`, arm64-apple-darwin |
| C+ compiler | `build/c+` → `C+ 0.1.0` |
| Rust | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| timeout | `/usr/bin/timeout 30` |
| shell | `/bin/bash` |

## Exact build commands

```sh
# cgame.c (plain C; original README suggests gcc, clang compiles it unchanged)
/usr/bin/clang -std=c11 -O2 -o build/orig_cgame ../../../examples/apps/cgame.c

# student.cp (see "Build-flag deviation" below)
../../../build/c+ build ../../../examples/apps/student.cp \
    -o build/orig_students --legacy-c --sanitize=none

# macpulse.cp requested flags -- fail on this branch; exact evidence below
../../../build/c+ build ../../../examples/system/macpulse.cp \
    -o build/orig_macpulse_unpatched --strict --sanitize=none     # exit 1

# minimally patched copy of the same source (no examples/ edit)
ln -sfn ../../../stdlib stdlib
cp ../../../examples/system/macpulse.cp macpulse_patched.cp
perl -pi -e 's/v->len\(\)/v->size()/g; s/\.push\(/.push_back(/g;' macpulse_patched.cp
perl -0pi -e 's/for \(int i = 0; i < this->proc_count; \+\+i\)\n            this->top\[i\] = entries\[i\];/for (int j = 0; j < this->proc_count; ++j)\n            this->top[j] = entries[j];/' macpulse_patched.cp
( cd . && ../../../build/c+ build macpulse_patched.cp \
      -o build/orig_macpulse --legacy-c --sanitize=none )

# Rust equivalents
rustc -O cgame.rs    -o build/rust_cgame
rustc -O student.rs  -o build/rust_student
rustc -O macpulse.rs -o build/rust_macpulse
```

## Deterministic run method

Every case runs in a **fresh working directory** with
`HOME="$PWD" NO_COLOR=1`, stdin from a fixed input file, and
`timeout 30`. Compared byte-for-byte: stdout, stderr, exit status, and
(for `student`) the final `students.db`. `cgame`'s progress file is reset by
the fresh `HOME`. All inputs/captures/diffs are under `out/`.

| example | deterministic input (`\n` = newline) |
| --- | --- |
| `cgame/quit` | `q` |
| `cgame/invalid` | `x`, `q` |
| `cgame/reset` | `r`, `q` |
| `cgame/levels_back` | `1`, `b`, `q` |
| `cgame/libmenu` | `2`, `b`, `q` |
| `cgame/quiz_wrong` | `1`, `1`, (empty), `wrong` ×4, `b`, `q` |
| `cgame/quiz_correct` | `1`, `1`, (empty), `b`, `main`, `c`, `3`, `s`, `b`, `q` |
| `cgame/libex0` | `2`, `0`, (empty), `q`, `b`, `q` |
| `student/crud` | `1`, `1`, `Alice`, `20`, `95.5`, `2`, `3`, `1`, `4`, `1`, `Alicia`, `21`, `88.25`, `5`, `1`, `9`, `0` |
| `student/persist` | `1`, `1`, `Bob`, `30`, `88.25`, `1`, `2`, `Carol`, `25`, `0.5`, `0`, `2`, `0` |
| `macpulse` | empty stdin (piped, non-TTY) |

## Results

`./compare.sh` prints exactly:

```
EXAMPLE_COMPARE cgame MATCH 8/8 deterministic menu/quiz/sandbox paths byte-identical (stdout, stderr, exit); deeper level content not modeled
EXAMPLE_COMPARE student MATCH 2/2 deterministic CRUD+persistence paths byte-identical (stdout, stderr, exit, students.db)
EXAMPLE_COMPARE macpulse MATCH non-interactive tty-guard path byte-identical (stderr + exit 1) via minimally patched copy; live interactive sampling is timing/hardware-dependent and BLOCKED for byte comparison (Rust --model is deterministic)
TOTAL: 3/3 examples matched on the tested deterministic paths
```

| example | status | cases | compared | evidence |
| --- | --- | --- | --- | --- |
| `examples/apps/cgame.c` | MATCH | 8/8 | stdout, stderr, exit | `out/cgame.*.stdout.diff` (all empty), `out/compare.run.log` |
| `examples/apps/student.cp` | MATCH | 2/2 | stdout, stderr, exit, `students.db` | `out/student.*.stdout.diff` (all empty), `out/student.*.orig.stdout` |
| `examples/system/macpulse.cp` | MATCH (guard) / BLOCKED (live) | 1/1 guard | stderr, exit | `out/macpulse.*.diff` (empty), `out/macpulse.unpatched.build.log` |

**Totals:** 3/3 examples MATCH on the tested deterministic paths.
Byte-comparable stream/exit checks executed: 11 (8 cgame + 2 student + 1 macpulse), all identical.
Additional checks: 2× `students.db` byte-identical.

## Build-flag deviation (exact evidence)

The requested `--strict --sanitize=none` build does **not** compile the two
legacy `.cp` examples on this branch:

* `student.cp` with `--strict --sanitize=none` exits 1 with 16
  `[CPLUS-LIFETIME] variable '…' is uninitialized` errors on out-parameters
  written through pointers (`end`, `id`, `choice`, `buf`), e.g.:

  ```
  examples/apps/student.cp:44:13: error: [CPLUS-LIFETIME] variable 'end' is uninitialized (read before it is initialized)
  ```

  Saved in `out/student.strict-attempt.build.log`.
  `--default --sanitize=none` fails identically; plain default mode also
  fails while building the instrumented C+ runtime
  (`CPLUS_EXIT_ARITHMETIC` undeclared). `--legacy-c --sanitize=none` builds
  successfully, so that mode was used for the original.
* `macpulse.cp` with `--strict --sanitize=none` exits 1 with (exact log
  `out/macpulse.unpatched.build.log`):

  ```
  .../examples/system/macpulse.cp:129:13: error: call to undeclared function 'vector_len'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
  .../examples/system/macpulse.cp:370:13: error: call to undeclared function 'vector_push'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
  2 errors generated.
  ```

  Cause: branch API drift — `stdlib/vector.hp` now exposes `size()`,
  `push_back()` instead of the example's `len()`, `push()`. With
  `--legacy-c` there is an additional C89-scope `redefinition of 'i'`.
  A 3-site mechanical patch (2 renames + 1 loop-variable rename, shown in
  the diff below and regenerated by `compare.sh`) yields a runnable binary
  with identical logic. No `examples/` file was edited.

Minimal patch applied to the copy (`build/orig_macpulse`):

```diff
-    int n = v->len();
+    int n = v->size();
-        for (int i = 0; i < this->proc_count; ++i)
-            this->top[i] = entries[i];
+        for (int j = 0; j < this->proc_count; ++j)
+            this->top[j] = entries[j];
-            cpu_history.push((int)(monitor.cpu + 0.5));
-            ram_history.push((int)(monitor.mem_percent + 0.5));
+            cpu_history.push_back((int)(monitor.cpu + 0.5));
+            ram_history.push_back((int)(monitor.mem_percent + 0.5));
```

## BLOCKED evidence — genuinely untestable behavior

`macpulse`'s live interactive monitor cannot be byte-compared in this
harness, for two independent reasons:

1. **TTY precondition.** With piped stdin/stdout the original takes the
   deterministic guard path and emits only
   `MacPulse needs an interactive terminal.\n` on stderr with exit 1; no
   sampling or rendering occurs. This is the path that was compared and
   matched.
2. **Hardware/timing dependence when a TTY is present.** `Monitor::sample()`
   calls `host_statistics`, `host_statistics64`, `sysctlbyname("hw.memsize")`,
   `statfs("/")`, `getifaddrs`, `proc_listpids`/`proc_pidinfo`/`proc_name`,
   `getloadavg` and `gettimeofday`; the loop sleeps 50 ms and samples once
   per `period_ms`. PID lists, resident sizes, byte counters, uptime and
   wall-clock timing are different on every invocation, so no two runs —
   original or Rust — can produce identical output on that path. There is
   also no way to drive `isatty()==1` for both fds from the non-interactive
   `timeout 30` harness without a PTY.

The Rust `--model` flag exercises a deterministic render/key loop with fixed
sample values (`cpu=12.5`, `mem_percent=45.0`, 8 cores, 321 processes,
90-column terminal, 3 synthetic top processes). It demonstrates the mapped
behavior (units/rate/bar/spark formatting, `[PAUSED]`, `+/-` refresh, `1/2`
page switching, `q` quit message) but is explicitly **not** claimed to match
live hardware output.

## Explicit non-equivalence statement

These Rust programs are **behavioral models for the tested deterministic
paths, not source-equivalent ports**. They are non-equivalent in these
documented ways:

* **cgame** — the engine state machine, banner, all menus, level 1's full
  quiz/lesson, Ex00's full theory, the ex01 sandbox prompts and progress
  persistence are reproduced. The static teaching corpus of levels 2–9 and
  Ex01–Ex09 (lessons, questions, hints, stubs, test mains) is *not* replicated:
  entering those paths in Rust prints the level-1 quiz data with the level-N
  header. The Rust sandbox models the untouched-stub `✘ It doesn't compile
  yet.` branch and does not invoke `gcc`/`timeout` or read `exercises/` edits.
  With colors enabled (no `NO_COLOR`) both use the same ANSI constants, but
  only the `NO_COLOR=1` path was byte-compared.
* **student** — prompts, validation, CRUD ordering, column formats and the
  SQLite-free `students.db` format match byte-for-byte on tested inputs. The
  `%.17g`/`%g` formatters are hand-rolled approximations adequate for the
  tested values (e.g. `95.5`, `88.25`, `0.5`); exotic floats (`NaN`, huge
  exponents) and the `perror` text on I/O failure are not guaranteed to match.
* **macpulse** — only the non-TTY guard matches byte-for-byte. The Rust
  program does not read Mach/sysctl/libproc/ifaddrs/statfs state, does not set
  termios raw mode, does not emit the alternate-screen entry/exit sequences
  on the real interactive path, and does not sleep. Its `--model` mode is a
  deterministic visualization model, not a monitor.

No claim of live-game or live-hardware success is made from these build or
emulator/model results.

# Handoff — P1 "Thread-safety analysis — prevent data races"

Agent 1 of 12 (`agent/modern-stdlib`). This directory contains **minimal,
evidence-only** material. No compiler, runtime, header, test runner, or any file
outside `tests/stdlib/agent1_handoff/thread_safety/` was created or modified.
No git add/commit was performed. **No binary produced from these four cases was
built or run; no real race was executed.** All evidence is compile-time (`c+
check`) plus tool-capability probes, recorded verbatim in `results/`.

## Environment / toolchain

- `./build/c+ --version` → `C+ 0.1.0`, `Backend: host Clang/LLVM` (exit 0).
  Raw output: `results/cplus-version.log`.
- Sanitizer CLI values are validated: `check --strict --sanitize=bogus` →
  `error: invalid sanitizer 'bogus' (expected none|address|undefined|memory|thread|leak|cfi|integer)`, exit 1
  (`results/sanitize-bogus-control.log`). `thread` is therefore a **recognised**
  CLI value, not a typo.

## Cases (all include `stdlib/thread.hp`; all are CHECK-ONLY)

| File | Unsafe shape |
| --- | --- |
| `race_shared_vector.cp` | Two `thread`s call `vector::push_back` on one shared `vector` with no guard. |
| `race_reader_writer_string.cp` | One thread reads `string::c_str()/size()` while another calls `string::drop()` on the same object. |
| `race_shared_file_write.cp` | Two threads call `file::write` on the same `file_borrow(stdout)` borrowed `FILE*`. |
| `race_mutex_not_held.cp` | A `mutex` is created but no `lock()`/`guard()` is held when two threads mutate a shared counter. |

## Exact results

Command run for each case (raw stdout+stderr and exit in `results/<name>.check-strict.log`):

```
./build/c+ check --strict tests/stdlib/agent1_handoff/thread_safety/<case>.cp
```

| Case | stdout (verbatim) | exit | `[CPLUS-` diagnostic | classification |
| --- | --- | --- | --- | --- |
| `race_shared_vector.cp` | `check: tests/stdlib/agent1_handoff/thread_safety/race_shared_vector.cp ok` | 0 | none | **ACCEPT — capability gap** |
| `race_reader_writer_string.cp` | `check: tests/stdlib/agent1_handoff/thread_safety/race_reader_writer_string.cp ok` | 0 | none | **ACCEPT — capability gap** |
| `race_shared_file_write.cp` | `check: tests/stdlib/agent1_handoff/thread_safety/race_shared_file_write.cp ok` | 0 | none | **ACCEPT — capability gap** |
| `race_mutex_not_held.cp` | `check: tests/stdlib/agent1_handoff/thread_safety/race_mutex_not_held.cp ok` | 0 | none | **ACCEPT — capability gap** |

**Meaning:** strict compile-time checking lowers and accepts all four
genuinely unsafe concurrent programs, emitting no tagged diagnostic. C+ does
not currently detect unsynchronised shared-state access, cross-thread
use-after-free, concurrent writes through a borrowed `FILE*`, or a held/not-held
mutex discipline. This matches the pre-existing record in
`tests/stdlib/compile_time/thread.json` (its four unsafe shapes are `ACCEPT`,
"RUNTIME" expectations) and `tests/stdlib/agent2_audit/module_audits/thread.hp.md`
§7 items 6–8 (multi-threaded contention `[UNTESTED]`; no ASan on this host).

## Thread-sanitizer configuration through build/c+

- `check --strict --sanitize=thread <case>` → `check: ... ok`, exit 0
  (`results/sanitize-thread-check.log`). The `check` stage validates the flag
  value but does not instrument anything; this is not dynamic evidence.
- `build --strict --sanitize=thread <case> -o /dev/null` →
  `error: sanitizer 'thread' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe`,
  exit 1 (`results/sanitize-thread-build.log`). This was a **compile-only**
  probe (the driver's own internal probe); the produced binary was never run.

**Conclusion:** no ThreadSanitizer dynamic evidence is obtainable through
`build/c+` on this host (macOS/arm64, `/usr/bin/clang`). `--sanitize=thread` is
a valid CLI value but unsupported at build time. Compile-time analysis evidence
is the only obtainable signal here.

## What static/dynamic analysis Agent 1 must provide

**Static (compile-time) analysis**
1. Detect the four shapes above and emit a tagged diagnostic (e.g.
   `[CPLUS-RACE]`) that `--strict` promotes to an error. Target `expect: REJECT`.
2. Track synchronisation state: distinguish *unguarded* shared access from
   access under a live `mutex`/`guard` actually held across the access, so the
   new check does **not** regress the valid fixtures
   `tests/stdlib/compile_time/thread_valid_guard.cp`,
   `tests/stdlib/compile_time/thread_valid_lock.cp`, and
   `tests/stdlib/deep_thread_task5.cp` (which locks a real mutex).
3. Model cross-thread lifetime/ownership: flag `drop()`/`stdlib_release()` on
   an object that another thread may read (the string case), not only the
   single-thread use-after-free forms C+ already tracks (`p.len`,
   `cplus_check_live(p)`).
4. Model borrowed-resource aliasing: two `file*` borrows of the same underlying
   `FILE*` must be treated as one synchronisation target (the `file::write`
   case), since `file_borrow()` returns a new wrapper with `owns = 0`.

**Dynamic analysis**
5. Add host support for `--sanitize=thread` (or an equivalent runtime race
   detector) in `build/c+`; currently `build --strict --sanitize=thread` exits 1
   on this host, so no TSan report can be recorded.
6. Provide bounded, isolated runtime regression fixtures for the guarded
   positive controls so the false-positive rate of any new static race check is
   measurable. Reuse the forked `ERRORCHECK`-mutex / watchdog fault-injection
   pattern from `tests/stdlib/deep_thread_guard.cp` and
   `tests/stdlib/deep_thread_nested.cp`; do not run unguarded races in the main
   harness, which is why these four cases were left check-only.

## Evidence integrity

- All stdout/exit strings above were produced by the recorded commands on
  `branch agent/modern-stdlib` in this workspace; nothing is fabricated.
- Raw logs are in `results/`. The four `.cp` files describe real races but were
  only passed to `c+ check`, which lowers/type-checks without writing or
  executing an executable.

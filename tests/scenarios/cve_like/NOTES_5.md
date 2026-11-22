# CVE-like scenarios 13 & 15 -- safe variants and evidence

Scope: OOB jump-table dispatch (case 13) and thread reset/free race (case 15).
Only the four files in this directory were created/modified. No git add/commit.

## Toolchain notes (important for reproducing the evidence)

- Default `clang` here is Homebrew clang 23, which looks for a non-existent
  `MacOSX27.sdk` and fails without an explicit sysroot:
  `clang: warning: no such sysroot directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk'`
  followed by `fatal error: 'unistd.h' file not found`.
- All sanitizer runs below use
  `clang -fsanitize=... -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`
  (Homebrew clang 23). With that flag both ASan and TSan work.
- Apple clang 16 (`/usr/bin/clang`) is usable for the normal builds (it finds
  the SDK automatically), but its sanitizer runtimes are broken in this image:
  - ASan: `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)" (0x0, 0x0)`
  - TSan: `ERROR: Interceptors are not working. This may be because ThreadSanitizer is loaded too late (e.g. via dlopen).` ... `"interceptors not installed" && 0`
  So the baseline/safe sanitizer evidence uses Homebrew clang, not Apple clang.

## Baseline evidence (original crashing cases -- baseline only, never in the gate)

### case 13 -- ASan, input `3`

Command:
```
clang -fsanitize=address -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -O0 -g -std=c99 tests/scenarios/cve_like/case_13.c -o case13_asan
printf '3\n' | ./case13_asan
```
Exit code **134**. Exact key lines:
```
==1944==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016bbee338 at pc 0x000104213a80 bp 0x00016bbee2f0 sp 0x00016bbee2e8
READ of size 8 at 0x00016bbee338 thread T0
    #0 0x000104213a7c in main case_13.c:7
Address 0x00016bbee338 is located in stack of thread T0 at offset 56 in frame
    [32, 56) 'd' (line 7) <== Memory access at offset 56 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow case_13.c:7 in main
```
Also observed: `cmd=99` reads far past the redzone and jumps to garbage,
producing exit code **132** (SIGILL) with no ASan report -- i.e. a raw
control-flow hijack, not just an OOB read.

### case 15 -- ThreadSanitizer, input `1`

Command:
```
clang -fsanitize=thread -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -O1 -g -std=c99 -pthread tests/scenarios/cve_like/case_15.c -o case15_tsan
printf '1\n' | ./case15_tsan
```
Exit code **134**. Exact output:
```
==================
WARNING: ThreadSanitizer: data race (pid=3796)
  Write of size 8 at 0x000102d88000 by main thread:
    #0 main case_15.c:10 (case15_tsan:arm64+0x100003de8)

  Previous read of size 8 at 0x000102d88000 by thread T1:
    #0 worker case_15.c (case15_tsan:arm64+0x100003e3c)

  Location is global 'active' at 0x000102d88000 (case15_tsan+0x100008000)

  Thread T1 (tid=16281251, running) created by main thread at:
    #0 pthread_create <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64+0x32a20)
    #1 main case_15.c:9 (case15_tsan:arm64+0x100003d68)

SUMMARY: ThreadSanitizer: data race case_15.c:10 in main
==================
1 to reset: ThreadSanitizer: reported 1 warnings
```

## Baseline vs safe table

| Case | Hazard | Original (tool / input) | Original result | Safe variant | Safe result |
|------|--------|--------------------------|-----------------|--------------|-------------|
| 13 | OOB read of 3-entry function-pointer table, then indirect call | ASan / `cmd=3` | `stack-buffer-overflow READ of size 8` at `case_13.c:7`, rc 134; `cmd=99` jumps to garbage, rc 132 | `case_13_safe.c`: caller-side `cmd < 3` check before `d[cmd]()` | `cmd=99` -> `OUTCOME: rejected cmd=99 (>= 3); no out-of-bounds dispatch (safe)`, rc 0, clean under ASan; `cmd=1` -> `OUTCOME: dispatched cmd=1 within [0,3) (safe)`, rc 0 |
| 15 | Data race / use-after-free on global `active` between worker and reset | TSan / `1` | `WARNING: ThreadSanitizer: data race` on global `active` (`case_15.c:10` vs `worker`), rc 134 | `case_15_safe.c`: one `pthread_mutex_t` guards pointer read, deref, NULL-store and free; `stop` flag + `pthread_join` | `OUTCOME: thread joined, no reset/free race, resets=1 (safe)`, rc 0; 500 resets under TSan -> 0 warnings, rc 0 |

Safe builds were exercised in **both layers**:
- `safecrt` (default): `stdlib/safe/safe_c.h` + `libcplus_safecrt.a` + `libcplus_hardened.a`
- `plain`: `-DPLAIN_LIBC`, `cplus_libc_safe.h` + `libcplus_hardened.a`

Both layers produced identical, exit-0 outcomes.

## Runner output (actual)

```
$ sh tests/scenarios/cve_like/run_cve_like.sh
ok   case_09_safe (safecrt): OUTCOME: bounded heap window used safely (len=1)
ok   case_09_safe (plain): OUTCOME: bounded heap window used safely (len=1)
ok   case_11_safe (safecrt): size: OUTCOME: bounded counter write completed (value=123)
ok   case_11_safe (plain): size: OUTCOME: bounded counter write completed (value=123)
ok   case_12_safe (safecrt): bio: OUTCOME: bounded bio copy completed (len=1)
ok   case_12_safe (plain): bio: OUTCOME: bounded bio copy completed (len=1)
ok   case_13_safe (safecrt): OUTCOME: dispatched cmd=1 within [0,3) (safe)
ok   case_13_safe (plain): OUTCOME: dispatched cmd=1 within [0,3) (safe)
ok   case_15_safe (safecrt): OUTCOME: thread joined, no reset/free race, resets=1 (safe)
ok   case_15_safe (plain): OUTCOME: thread joined, no reset/free race, resets=1 (safe)
cve-like safe variants: 10 passed, 0 failed
```
Runner exit status 0. It builds both libraries, builds every `*_safe.c` in this
directory in both layers, runs each with input `1`, and requires exit 0. The
original `case_13.c` / `case_15.c` are **not** built or run by the gate. The
extra `case_09/11/12_safe.c` entries were contributed by parallel agents into
the same directory; the glob picks them up automatically and they pass. An
earlier run, before those files existed, reported
`cve-like safe variants: 4 passed, 0 failed`.

## Classification

**NOT-FIXABLE-BY-LIBC -- caller-side fix required**, for both cases.

- Case 13: neither the SafeCRT nor the plain-libc layer changes the semantics of
  `d[cmd]()`. Those layers harden allocation, memory copies and formatted I/O;
  they cannot know the extent of a caller-owned stack array used as a jump
  table, and they do not instrument an indirect call's index. Only the caller
  can establish `cmd < 3` before dispatch, which is exactly what
  `case_13_safe.c` does.
- Case 15: no libc wrapper serializes two threads' access to a caller-owned
  pointer. The race is on the program's own `active` global and on the lifetime
  of the block it points to. `calloc_s`/`cplus_calloc` track the block but do
  not make the unsynchronized read/free pair atomic. `case_15_safe.c` adds the
  missing synchronization with a pthread mutex and a stop flag, so the worker
  never dereferences a pointer main may have freed, and is joined before exit.

The safe headers are still included in both variants (default and `-DPLAIN_LIBC`)
so the variants build under the project's two supported layers, but the safety
of these two specific hazards comes from the explicit caller-side logic.

## Residual limitations

- **Case 13**: the bound `3` is written explicitly (with `count` derived from
  the table as a second guard); if the table is later extended, the literal
  must be updated. The check protects the dispatch index only -- it is not CFI
  or a shadow stack, so a table corrupted by some *other* bug would still be
  followed. `scanf("%zu")` accepts negative-looking input as a huge unsigned
  value, but that is rejected by the same `cmd >= 3` check.
- **Case 15**: the worker is a spin loop (uses CPU while active); the mutex
  serializes every increment, so this is correctness, not throughput. The
  `stop` flag is a plain `int` but is only ever accessed under the same mutex,
  so it is race-free. This is a lock/flag solution, not a refcount or epoch
  reclamation scheme; if a future worker body blocks while holding the lock,
  resets would stall. The safe allocator has its own independent tracking
  mutex, which is not contended here because only the main thread allocates
  or frees.
- Sanitizer evidence was produced with Homebrew clang + explicit `-isysroot`;
  Apple clang's sanitizer runtimes are broken in this image (exact errors
  above). The normal gate uses Apple clang, which builds and runs correctly.
- The original crashing files remain in the directory purely as baseline
  evidence and must not be promoted into the normal gate.

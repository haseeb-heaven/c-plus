# Issue #245 driver regressions

Covers all three root causes from GitHub issue #245.

## Dangling short options (the SIGSEGV)

`compiler/Main.cpp` printed `error: -I requires a value` but kept parsing and
read `argv[argc]` (NULL). It now returns 1 immediately. `run_suite.py` runs
each of `-I/-D/-U/-L/-l` with no value (both bare and after `build`), asserts
exit status 1 and the exact diagnostic, and treats any signal death (negative
returncode) or crash banner as a FAIL. No clang or sanitizer runtime needed.

## Probe fd leak and /tmp symlink TOCTOU

`sanitizerSupported()` derived `compileLog`/`runLog` from the executable's
predictable temp name. All four probe files are now created under independent
O_EXCL names and RAII-owned, and the source descriptor is written before any
later create can fail, so `probeFd` cannot leak.

## Leaked temporary object files

The instrumented-runtime `.o` and each per-input `.o` were only recorded in
the link list, never removed. Both are now pushed to the `TempFile` guard
vector, so they are deleted on success and on every early return.

## Run

```sh
python3 tests/compiler/driver_issue_245/source_guard.py
CPLUS_BIN=$PWD/build-issue245/c+ python3 tests/compiler/driver_issue_245/run_suite.py
```

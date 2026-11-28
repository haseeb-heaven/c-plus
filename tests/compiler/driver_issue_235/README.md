# Issue #235 driver regressions

Covers both halves of GitHub issue #235 in `compiler/Driver/Driver.cpp`.

## Gate bypasses

`normalizeFlag()` folds `--foo` into `-foo`, so a short-option prefix test on
the normalized token let `--ld-path=...` impersonate `-l...` (arbitrary linker)
and let `--language=c++` pass STRICT (clang compiles C++ while the safety AST
sees C). The allow-list now tests `-I`/`-D`/`-L`/`-l` on the RAW token, the
language selector is recognized in both spellings by `stripLanguageFlags()` and
`extraArgsChangeParse()`, and `--std=` is rejected when it cannot be forwarded
to the safety AST.

## Probe temp files

`sanitizerSupported()` no longer builds shell command strings with
`<temp>.log` redirection. All four files are created with O_CREAT|O_EXCL and
mode 0600, the compiler and probe are launched with `ExecuteAndWait` plus
redirects, and `TempFile` removes every file on every exit path.

## Run

Static structural guard (needs no build, no compiler):

```sh
python3 tests/compiler/driver_issue_235/source_guard.py
```

Behavioral cases (needs a built `c+`; hard timeout, exact exit status and
diagnostic substrings):

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/driver_issue_235/run_suite.py
```

`fixture.c` is a clean TU: the only variable between cases is the injected
pass-through flag, so each rejection is attributable to the gate under test.
No sanitizer probe runs (`--sanitize=none`), making the suite deterministic on
hosts without sanitizer runtimes.

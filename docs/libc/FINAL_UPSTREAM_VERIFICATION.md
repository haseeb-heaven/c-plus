# Final libc/CRT verification after upstream integrations

Run: Mon Nov 23 2026 (branch `agent/modern-stdlib`).
Suite run serially from repo root with `CPLUS_CLANG=/usr/bin/clang` and `SDKROOT`
unset in each subshell. Compiler at `/usr/bin/clang` (Apple clang 16.0.0,
clang-1600.0.26.6, arm64-apple-darwin27.2.0). No source files were edited; this
document is the only artifact produced by this verification pass.

## Headline result

**NOT GREEN overall.** The three libc/safe-CRT suites pass cleanly. The two
suites that drive the C+ compiler front end (`tests/crt/run_all_crt.sh` and
`tests/run_tests.sh`) FAIL because of the known **external
`CPLUS_EXIT_ARITHMETIC` runtime/header mismatch** in the sibling
`c-plus-core` repository. This is an out-of-tree toolchain defect, not a libc or
CRT regression; it is reproduced here verbatim. No claim of a green compiler
suite is made.

## Exact results (run order)

| # | Command | Exit code | Final line |
|---|---------|-----------|------------|
| 1 | `CPLUS_CLANG=/usr/bin/clang sh tests/libc/build_libc.sh` | **0** | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a` |
| 2 | `CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh` | **0** | `libc tests: 91 passed, 0 failed` |
| 3 | `CPLUS_CLANG=/usr/bin/clang sh tests/crt/unit/run_unit_tests.sh` | **0** | `crt unit tests: 83 passed, 0 failed` |
| 4 | `CPLUS_CLANG=/usr/bin/clang sh tests/crt/run_all_crt.sh` | **1** | `  - strncpy_ops: C+ build failed (see build/crt/strncpy_ops.cplus.log)` |
| 5 | `CPLUS_CLANG=/usr/bin/clang sh tests/run_tests.sh` | **1** | `  - classic_bug9_overflow: compilation failed; see tests/out/classic_bug9_overflow.build.log` |

### Detail

1. **`build_libc.sh` — PASS (exit 0).** stdout is a single line, the path to the
   produced static archive; stderr empty. The `upstream_safecrt.c` translation
   unit compiled without warnings.

2. **`run_libc_tests.sh` — PASS (exit 0).** `libc tests: 91 passed, 0 failed`;
   stderr empty. The new upstream SafeCRT test ran and passed:
   `ok test_safecrt_upstream.c` (line 60 of stdout).

3. **`run_unit_tests.sh` — PASS (exit 0).** `crt unit tests: 83 passed, 0
   failed`; stderr empty.

4. **`run_all_crt.sh` — FAIL (exit 1).** `set -eu` aborts at the CRT conformance
   corpus stage; the closing `== ALL CRT CHECKS PASSED ==` banner is **not**
   reached. Body summary/final lines:

   ```
   CRT corpus: 0 passed, 10 failed

     - adversarial_defined: C+ build failed (see build/crt/adversarial_defined.cplus.log)
     - alloc: C+ build failed (see build/crt/alloc.cplus.log)
     - contract_violations: C+ build failed (see build/crt/contract_violations.cplus.log)
     - format_ops: C+ build failed (see build/crt/format_ops.cplus.log)
     - memory_ops: C+ build failed (see build/crt/memory_ops.cplus.log)
     - memset_ops: C+ build failed (see build/crt/memset_ops.cplus.log)
     - printf_ops: C+ build failed (see build/crt/printf_ops.cplus.log)
     - stdio_ops: C+ build failed (see build/crt/stdio_ops.cplus.log)
     - string_ops: C+ build failed (see build/crt/string_ops.cplus.log)
     - strncpy_ops: C+ build failed (see build/crt/strncpy_ops.cplus.log)
   ```

   Native facility probe (unaffected, printed for context): `fortify=2`,
   `optimized=1`, `annex_k=none`, `object_size=yes`,
   `dynamic_object_size=yes`, `fortified_memcpy=yes`,
   `checked_arithmetic=yes`, `object_size_of_32=32`, `platform=apple`,
   `glibc=no`, `ucrt_secure=unavailable`, `sizeof_size_t=8`.

5. **`run_tests.sh` — FAIL (exit 1).** Summary line at stdout line 152:
   `C+ tests: 99 passed, 51 failed`. All 51 failures are `compilation failed`
   against the instrumented C+ runtime (the trailing `FAILED` list ends with
   `  - classic_bug9_overflow: compilation failed; see tests/out/classic_bug9_overflow.build.log`).
   stderr empty.

## Exact failure diagnostic (external runtime mismatch)

Representative log `build/crt/alloc.cplus.log` (identical root cause in all 10
CRT-corpus logs and in the failing `tests/out/*.build.log` files):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  130 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:137:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  137 |   cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, kind, NULL, 0, msg);
      |                     ^
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:245:23: error: use of undeclared identifier 'CPLUS_EXIT_CONVERSION'; did you mean 'CPLUS_EXIT_OWNERSHIP'?
  245 |     cplus_print_fatal(CPLUS_EXIT_CONVERSION, "implicit integer conversion",
      |                       ^~~~~~~~~~~~~~~~~~~~~
      |                       CPLUS_EXIT_OWNERSHIP
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/../runtime/include/cplus/cplus.h:76:3: note: 'CPLUS_EXIT_OWNERSHIP' declared here
   76 |   CPLUS_EXIT_OWNERSHIP = 106,
      |   ^
3 errors generated.
error: failed to compile instrumented C+ runtime
```

`CPLUS_EXIT_ARITHMETIC` (and `CPLUS_EXIT_CONVERSION`) are referenced by the
external `c-plus-core/runtime/cplus_runtime.c` but are not declared in this
branch's `runtime/include/cplus/cplus.h`. Documented shim used elsewhere in the
repo: `-DCPLUS_EXIT_ARITHMETIC=110` (see
`tests/security/resource_exhaustion/NOTES.md` and `README.md`). That shim was
**not** injected here, because these runner scripts were invoked exactly as
specified, in order to report the unmodified status.

## Upstream `upstream_safecrt.c` integration check

- **Source present:** `libc/src/string/upstream_safecrt.c` (9,690 bytes).
- **In `tests/libc/build_libc.sh`: YES.** Line 8's source list includes
  `$ROOT/libc/src/string/upstream_safecrt.c` (verified by grep: the token
  `upstream_safecrt.c` appears in line 8; step 1 above compiled it successfully).
- **Test in `tests/libc/run_libc_tests.sh`: YES (via glob).** Line 10 iterates
  `for t in tests/libc/test_*.c`, so `tests/libc/test_safecrt_upstream.c`
  (5,486 bytes) is included. The only skips are `*_attack.c` and
  `*/test_corpus_format.c` (line 11), which do not match this test. Step 2
  above shows `ok test_safecrt_upstream.c`, confirming it was compiled, linked,
  and executed with a zero exit status.

**No gap found** for the upstream SafeCRT source/test integration.

## Conclusion

- libc build, libc tests (91/0), and safe-CRT unit tests (83/0) are GREEN —
  including the new upstream SafeCRT source and its test.
- The compiler-driven CRT corpus and C+ test suite are RED solely because of the
  pre-existing external `CPLUS_EXIT_ARITHMETIC` / `CPLUS_EXIT_CONVERSION`
  runtime/header mismatch in `c-plus-core`. They must not be reported as green
  until that out-of-tree toolchain mismatch is resolved (or the documented
  `-DCPLUS_EXIT_ARITHMETIC=110` shim is applied by those runners).

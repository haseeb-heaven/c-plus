# HIGH-finding consolidated regression suite

Date: 2026-11-23. Owner: audit regression agent (branch `agent/modern-stdlib`).

Suite: `tests/crt/unit/test_audit_high.c`
Summary line: `audit_high: <N>/<N> checks ok` (non-zero exit on any failed check).

## Scope

`docs/libc/WRAPPER_SAFETY_AUDIT.md` aggregates 16 HIGH findings. Several of
them already have a dedicated test (`test_fclose_s.c`, `test_uchar.c`,
`test_threads_c11.c`, `test_wchar_format.c`, `test_format_security_s.c`).
This suite is the single consolidated home for the HIGH findings that were
distributed across those files, plus the two MEDIUM hardening families the
work order explicitly asks to be exercised alongside them (signal/fenv NULL
guards and logger `errno` preservation, audit items 4–5 of §4).

The suite calls only the **existing hardened APIs**. It never dereferences a
freed pointer, forges a bad object, or invokes undefined behaviour in order to
"demonstrate" a historical bug. It asserts the documented post-fix contract.

## Coverage map

| Area | Contract source | API under test | Expected contract |
|---|---|---|---|
| Double-fclose, colliding `FILE*` | `AUDIT_stdio_format.md` #1/#2/#3 | `cplus_fclose_s` (SafeCRT), `cplus_fclose` (plain) | second close refused before any host `fclose`; tombstoned probe chains keep the surviving marker reachable; `errno=EINVAL` (`_s`) / `EBADF` (plain) |
| Detached start context | `AUDIT_lifecycle.md` F1 | `cplus_thrd_create` + `cplus_thrd_detach` | `{func,arg}` read from a heap cell; worker sleeps past the creating frame, which is poisoned with `0xA5`; payload arrives intact |
| uchar surrogate state | `AUDIT_lifecycle.md` F2 | `cplus_mbrtoc16/32`, `cplus_c16rtomb`, canary diagnostic | `sizeof(mbstate_t) >= sizeof(unsigned)`; host state is a real `mbstate_t`; surrogate word is separate storage; canaries survive split UTF-8 through caller and `ps==NULL` states |
| Huge-`n` string/memory guards | `AUDIT_memory_string.md` #2/#3 | `cplus_strncpy`, `cplus_strncpy_bounded`, `cplus_memchr`, `cplus_memrchr`, `cplus_stpncpy` | `n == SIZE_MAX` refused with `EOVERFLOW` (`ERANGE` for bounded) with **no** writes; NULL refused with `EINVAL`; small valid `n` keeps the documented copy/zero-fill semantics |
| Wide-format `%n` | `AUDIT_numeric_wide.md` F01 | `cplus_swprintf_checked` | `%n`, `%1$n`, `%lln` refused with `-1`/`EINVAL` before `va_start`; sentinel unchanged; `%%n` is a literal and is accepted; NULL fmt/dest refused; plain `cplus_swprintf` still rejects NULL fmt |
| Signal NULL guards | `AUDIT_lifecycle.md` F4 | `cplus_sigemptyset/fillset/addset/delset/ismember/sigsuspend/sigwait` | required pointer NULL → `-1`/`EINVAL`; valid sets still work |
| fenv NULL guards | `AUDIT_lifecycle.md` F5 | `cplus_fegetenv/fesetenv/fegetexceptflag/fesetexceptflag/feholdexcept/feupdateenv` | required pointer NULL → `-1`/`EINVAL`; real env can still be saved/restored |
| Logger `errno` | `AUDIT_lifecycle.md` F6/F7 | `cplus_libc_log` | `errno` restored on every exit path, including when `write(2)` fails (fd 2 closed) |

## Verification

Build both libraries, then compile the one suite against **both** and run it
plain and under UBSan:

```sh
cd <repo>
export CPLUS_CLANG=/usr/bin/clang           # default `clang` has a broken sysroot here
sh scripts/build_safecrt.sh
sh tests/libc/build_libc.sh

INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include \
     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
SAFE=build/safe/libcplus_safecrt.a
LIBC=build/libc/libcplus_hardened.a
mkdir -p build/audit_high

# plain
$CPLUS_CLANG -O2 -g -std=c99 -Wall -Wextra -pedantic $INC \
  tests/crt/unit/test_audit_high.c "$SAFE" "$LIBC" -lm -pthread \
  -o build/audit_high/test_audit_high_plain
timeout 120 build/audit_high/test_audit_high_plain

# UBSan
$CPLUS_CLANG -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -fno-sanitize=float-divide-by-zero $INC \
  tests/crt/unit/test_audit_high.c "$SAFE" "$LIBC" -lm -pthread \
  -o build/audit_high/test_audit_high_ubsan
timeout 120 build/audit_high/test_audit_high_ubsan
```

(When running from zsh, pass the include flags as an array; zsh does not
word-split an unquoted `$INC` scalar.)

## Results on this host

| Mode | Libraries | Result |
|---|---|---|
| plain, `-O2 -std=c99 -Wall -Wextra -pedantic` | `libcplus_safecrt.a` + `libcplus_hardened.a` | `audit_high: 113/113 checks ok`, exit 0 (10/10 repeated runs) |
| UBSan, `-O1 -std=c99 -fsanitize=undefined -fno-sanitize-recover=all` | both | `audit_high: 113/113 checks ok`, exit 0, no runtime errors (5/5 repeated runs) |
| plain, `LC_ALL=C` | both | `audit_high: 92/92 checks ok`; surrogate checks print a skip note (locale has `MB_CUR_MAX < 4`) |

The count difference (113 vs 92) confirms the UTF-8 surrogate branch is
actually exercised under the default `C.UTF-8` locale rather than silently
skipped.

## Warnings / integration notes

1. **Runner wiring is required.** `tests/crt/unit/run_unit_tests.sh` currently
   compiles every `test_*.c` with only `libcplus_safecrt.a` and without
   `-Ilibc/install-include`. `test_audit_high.c` needs both libraries and that
   include path. Add a case alongside the existing `*safe_c_api*` case, e.g.:

   ```sh
   *audit_high*) EXTRA_INC="-I. -Ilibc -Ilibc/install-include"; \
                 EXTRA_LIB="build/libc/libcplus_hardened.a" ;;
   ```

   This change is outside this agent's file ownership and was **not** made.
2. **Allocator-dependent collision setup.** To make the `FILE*` address reuse
   deterministic, the collision helper keeps its probe streams open while it
   wrapper-closes the colliding pair; releasing the probes first makes Darwin's
   allocator hand back an unrelated recycled address and the test flakes.
   Verified 10/10 stable after this adjustment.
3. **Fixes were in flight.** The wrappers exercised here were being changed by
   other agents on this branch. Every check above passed against the binaries
   built at verification time; no current-binary failure was observed. If a
   later fix regresses a contract, the failing `FINDING:` line identifies the
   source line and assertion.
4. **Not a 100% claim.** The suite covers the listed HIGH contracts only. The
   remaining findings in `WRAPPER_SAFETY_AUDIT.md` (MEDIUM/LOW, compiler-side
   contracts, dependency-closed upstream gaps) are out of scope here.

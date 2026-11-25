# wchar_attack — adversarial results

Target: `tests/libc/test_wchar_attack.c` (12 wide-char targets declared in
`libc/install-include/cplus_libc.h`). Environment: macOS 14/arm64, Apple
Clang 16, host libc. All wrappers are thin, logged, `NULL`-hardened
delegators — no bundled wchar implementation — so "wrapper vs host"
differential equality is the primary correctness property.

## Result

No wrapper defect that crashes, overruns a buffer, drops a required `NULL`
guard, returns a wrong hardening-failure value, or diverges from the host
oracle was found. All 8 attack groups complete; 1000-case fixed-seed
differential passes; UBSan is clean. `wchar_attack: ok`, exit 0, ~2.9 s.

## Observed behaviors / findings (exact)

1. **`FINDING` — `cplus_swprintf` huge field width is a CPU DoS vector.**
   `cplus_swprintf(buf, 8, L"%999999999ls", L"hi")` does not return within a
   2 s alarm in a child process (host `vswprintf` burns CPU, `SIGALRM`
   terminates the child). With a tractable width of 1,000,000 it returns
   `-1` / `errno=84` (EOVERFLOW) and leaves canaries at indices 8 and 15
   intact, so the `n` bound itself is honored. The wrapper delegates with no
   width guard; format strings are documented caller responsibility, so this
   is a hardening gap rather than a standard-conformance bug.

2. **`FINDING` — `cplus_mbrtowc` 0xFF under `"C"` locale does not yield
   EILSEQ on this host.** With `setlocale(LC_ALL,"C")`, `MB_CUR_MAX == 1`;
   `cplus_mbrtowc(&wc, "\xff", 1, &st)` returns `1`, `wc == 0xFF`,
   `errno == 0` (wrapper == host exactly). The attack-scenario expectation of
   `(size_t)-1`/EILSEQ holds only in a multibyte locale: after
   `setlocale(LC_CTYPE,"en_US.UTF-8")`, `MB_CUR_MAX == 4`, 0xFF returns
   `(size_t)-1` with `errno == EILSEQ` (92) and a truncated `0xC3` with `n=1`
   returns `(size_t)-2`. Test asserts both, so it is locale-portable.

3. **`FINDING` — `cplus_wcsftime` with `maxsize == SIZE_MAX`** returns `0`
   with `errno == EINVAL` on Apple (host rejects huge `n`); a canary buffer
   is untouched (no overflow). `maxsize == 0` (with and without a `NULL`
   destination), `maxsize == 1`, `NULL` format, and `NULL` destination with
   nonzero size all return defined values with no crash.

4. **`FINDING` — `%n` is not neutralized.** As documented in
   `wchar_format.c`, `cplus_swprintf(b,32,L"abcd%nEF",&n)` returns `6` and
   writes `4` through the supplied `int*`. Not a defect per the documented
   caller-responsibility contract, but a format-string attack surface.

5. **`FINDING` — `cplus_towctrans(L'a', (wctrans_t)0)`** returns `0x61`
   (`'a'`, unchanged) with `errno == EINVAL`; no crash, wrapper == host.
   Descriptor fabrication is host-defined and delegated verbatim.

## What held

- Overflow: `wcstoull`/`wcstoumax` saturate to `ULLONG_MAX`/`UINTMAX_MAX`
  with `ERANGE`; `wcstoll` saturates to `LLONG_MAX`; `wcstod/f/l` return the
  HUGE_VAL family with `ERANGE`.
- Invalid bases 1, 37, `INT_MAX`, `-1`: `0` / `EINVAL` / `endptr == input`.
- Empty input: `0` and `endptr == input`. NULL required string: `0` /
  `EINVAL` / `*endptr = NULL`.
- endptr forgery (mid-buffer, `NULL`, aliasing the input buffer): no crash,
  results and endptr offsets identical to host. Truly read-only targets are
  caller UB and were not passed.
- `wcstok`: empty delimiter, all-delimiter, trailing/adjacent delimiters,
  repeated `NULL` continuations, `NULL` saveptr continuation, and saveptr
  reset into a live buffer all terminate (bounded loop guard) with correct
  token sequences; `NULL` delim/saveptr are hardened to `EINVAL`/`NULL`.
- Restartable multibyte: invalid byte, truncation, embedded NUL, `NULL`
  state, `NULL` destination length query, required-endptr guards, round
  trips, and a deliberately confused/reused `mbstate_t` — all defined,
  no crash, wrapper == host.
- Wide format: `NULL` format, `NULL` stream, n=0/NULL destination,
  n>0/NULL destination — all hardened (`-1`/EOF, `EINVAL`); bounded writes
  with canaries for huge widths; `vswprintf` bounded too.
- `wctrans`: unknown property, empty string, embedded NUL, `NULL` all return
  `(wctrans_t)0` with `EINVAL` where required; valid `toupper` maps correctly.
- 1000-iteration fixed-seed LCG differential across `wcstoll`, `wcstoull`,
  `wcstoimax`, `wcstoumax`, `wcstod`, `towupper` — values, errno and endptr
  offsets all identical to host.

## Build / verify

```sh
sh tests/libc/build_libc.sh >/dev/null
clang -std=c99 -Wall -Wextra -pedantic -Ilibc -Ilibc/include \
  tests/libc/test_wchar_attack.c build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/test_wchar_attack && /tmp/test_wchar_attack
# UBSan: add -fsanitize=undefined -fno-sanitize-recover=all
```

Compile is warning-free. Runtime ~2.9 s (2 s is the deliberate SIGALRM
child probe for finding 1).

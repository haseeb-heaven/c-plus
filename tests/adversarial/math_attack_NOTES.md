# Adversarial math/complex/fenv attack — findings

Harness: `tests/libc/test_math_attack.c` (standalone C99, differential vs host
`<math.h>`/`<complex.h>`/`<fenv.h>`). Run with the provided verify command;
`main` restores the original rounding mode, clears exceptions and prints
`math_attack: ok`.

## FINDING 1 — `cplus_lroundf` / `cplus_llroundf` have the wrong return type

- `libc/install-include/cplus_libc.h:316-317` declares
  `float cplus_lroundf(float);` and `float cplus_llroundf(float);`
- `libc/src/math/math_float.c:52-53` defines them identically with `float`
  return (converting the `long`/`long long` result of the host call).
- ISO C requires `long lroundf(float)` and `long long llroundf(float)`; the
  sibling long-double wrappers (`math_ldouble.c:67-68`, header `:374-375`)
  correctly return `long`/`long long`, so the float family is inconsistent.
- Observed at `FLT_MAX`: wrapper returns `9.22337204e+18` as a `float` while
  host `lroundf`/`llroundf` return `9223372036854775807` (`LONG_MAX`/`LLONG_MAX`).
  The float round-trip loses precision; `long r = cplus_lroundf(x)` cannot
  represent the full integer range the ISO function promises.
- Classification is unaffected, but the public ABI/contract is wrong.

## FINDING 2 — `cplus_frexp`, `cplus_modf`, `cplus_remquo` (double family) crash on NULL out-param

- `libc/src/math/math2.c:22-23`:
  `double cplus_frexp(double x,int *e){ ... return frexp(x,e); }`
  `double cplus_modf(double x,double *ip){ ... return modf(x,ip); }`
- `libc/src/math/math4.c:10`:
  `double cplus_remquo(double x,double y,int *quo){ ... return remquo(x,y,quo); }`
- The `f` and `l` variants (`math_float.c:76-90`, `math_ldouble.c:81-104`) harden
  this case by setting `errno = EINVAL` and returning `0.0f`/`0.0L`. The unsuffixed
  variants do not, so `NULL` is passed straight to the host.
- Observed: forked child calling `cplus_frexp(8.0, NULL)`, `cplus_modf(8.0, NULL)`
  and `cplus_remquo(8.0, 3.0, NULL)` is killed by **SIGSEGV (signal 11)** —
  host `frexp`/`modf` write through the NULL pointer. No `EINVAL`, no `0` return.
- The harness forks these three probes so the SIGSEGV is reported, not fatal.

## What held (no divergence found)

- Hostile values for the float and long-double families (quiet NaN with payload
  via `nanf("0x1234")` / `nanl`, signaling NaN via `__builtin_nansf("")`,
  +/-Inf, `FLT_TRUE_MIN`/subnormals, `FLT_MAX`, signed zero, their negatives):
  `isnan` / `isinf` / `signbit` classification and tolerance value comparisons
  match the host for every entry point exercised (all 30 single-arg float
  wrappers, all 32 single-arg long-double wrappers, two-arg families, `fma*`,
  `nan*` tags, and integer-returning `lrint*`/`llrint*`/`ilogb*`).
- Domain/range (`sqrtf(-1)`, `logf(0)`, `logf(-1)`, `acosf(2)`,
  `powf(-2,0.5)`, `expf(1e10)`, `lgammaf(-1)`, `tgammaf(0)`, `atanhf(1)`,
  `remquof(x,0,&q)`): return classification, `errno` and FP flags match the
  host. Successful calls (`sqrtf(4)`, `powf(2,8)`, `sinf`, `logf`, `frexpf`)
  do not clobber `errno` — including with logging enabled.
- Out-params: NULL is rejected with `EINVAL`/0 for all `f`/`l` variants;
  normal and aliased/reused out-param storage produce host-identical results.
- FP state: with `FE_UPWARD` and a pending `FE_INVALID`, >20 wrappers leave
  the same exception flags and rounding mode as the corresponding host call,
  both with logging off and on. No silent flag clearing or mode changes.
- fenv: an all-bits-set fake `fexcept_t` passed to `cplus_fesetexceptflag`
  does not crash and matches the host return; `fegetexceptflag(NULL)` and
  `feholdexcept(NULL)` return nonzero with `EINVAL`; `feholdexcept` /
  `feupdateenv` round-trip (flags + rounding) matches the host.
- Complex: `csin`/`ccos`/`ctan`/`csinh`/`ccosh`/`ctanh`/`clog`/`cexp`/
  `csqrt`/`cproj` with NaN+Inf operands, `cpow(0,0)`, and representative `f`/`l`
  variants: no crash, host-identical classification/value/errno/flags.
- Logging toggled on/off: results are identical.

## Sanitizers

- Plain build: clean compile with `-Wall -Wextra -pedantic`; exit 0; ~0.06 s.
- `-fsanitize=undefined` (no exclusions): no UBSan `runtime error` diagnostics.
  The sanitizer prints `DEADLYSIGNAL` for the three forked NULL probes — that is
  the reported SIGSEGV, not harness UB. Use `UBSAN_OPTIONS=handle_segv=0` for
  clean output.
- With `-fno-sanitize=float-divide-by-zero` added: identical 4 findings, no
  additional diagnostics. This exclusion is **not needed** (no float-divide-by-
  zero reporting in this test); it was run only to satisfy the matrix.

# C23 `nextup` / `nextdown` — bare ISO APIs (six additions)

Date: 2026-11-23
Branch: `agent/modern-stdlib`
Toolchain: `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6),
target `arm64-apple-darwin27.2.0`.

## Result

The six APIs that `docs/libc/iso_c_inventory.json` classifies as `MISSING`
(`nextup`, `nextupf`, `nextupl`, `nextdown`, `nextdownf`, `nextdownl`) now have
real definitions and a standalone public header, with their exact ISO C
signatures and bare ISO names. The deterministic bit-pattern test passes
**84 checks / 0 failures** (also under UBSan), and the C ABI is unchanged:
exactly six **new** global symbols were added, with zero existing symbols
removed or retyped (nm evidence below).

## Provenance — no upstream source was copied

The task required checking the pinned upstream source first. On this checkout:

```sh
grep -rn -E 'nextup|nextdown' third_party/
# (no output)
# grep exit=1
```

`third_party/` contains no musl (and no other vendored libc) with these
functions at the pinned commit. Therefore this is **not** a copy of musl,
LLVM libc, glibc, or any other implementation — no such claim is made.

The concept is derived from the ISO C23 standard text for these functions
(ISO/IEC 9899:2024, 7.12.11.5 "The nextup functions" and 7.12.11.6 "The
nextdown functions"), which itself inherits the one-argument `nextUp` /
`nextDown` operations from WG14 TS 18661-1. The special cases implemented here
come from that text:

- `nextup(x)` is the next representable value greater than `x`;
  `nextdown(x)` is the next representable value less than `x`.
- `nextup(±0)` → least positive subnormal, `nextdown(±0)` → least negative
  subnormal (the requested *direction* wins over the sign of a zero).
- `nextup(least negative subnormal)` → `-0`;
  `nextdown(least positive subnormal)` → `+0`.
- `nextup(+inf) = +inf`, `nextup(-inf) = -TYPE_MAX`,
  `nextdown(-inf) = -inf`, `nextdown(+inf) = +TYPE_MAX`.
- A NaN is returned unchanged (payload preserved).

Reference texts consulted for the standard wording (fetched 2026-11-23):

- <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1615.pdf> — §14.4
  "The nextup and nextdown functions", the WG14 draft wording for 7.12.11.5
  and 7.12.11.6, including signed-zero and infinity cases.
- <https://www.open-std.org/JTC1/SC22/WG14/www/docs/n1664.pdf> — §14.4, the
  corresponding TS 18661-1 text.
- <https://libc.llvm.org/headers/math/index.html> — C23 section mapping
  (`nextup` = 7.12.11.5, `nextdown` = 7.12.11.6).

The actual algorithm is the **nextafter-based definition mandated by the
task**, not a transcribed upstream body:

```c
nextup(x)   = nextafter(x, +INFINITY);
nextdown(x) = nextafter(x, -INFINITY);
```

This is a thin `PLATFORM_BACKED` delegation to the C99 `<math.h>` `nextafter`
family: no builtin, no bit twiddling, and no fabricated algorithm. It realises
all of the standard special cases on this host, as the measurements below
show. Residual differences are recorded under **Deviations / limitations**.

## Files created — strict ownership (only these four)

| File | Purpose |
|------|---------|
| `libc/src/math/c23_nextup.c` (new) | The six bare-symbol definitions. |
| `libc/install-include/cplus_c23_nextup.h` (new) | Standalone public declarations. |
| `tests/libc/test_c23_nextup.c` (new) | Deterministic bit-pattern test. |
| `docs/libc/C23_NEXTUP.md` (new) | This record. |

No other repository file was created or modified; no `git add`/commit was
made; no build script, `CMakeLists.txt`, `runtime/**` or `compiler/**` was
touched. Build-list wiring is the parent agent's responsibility.

## Exact API surface

From `cplus_c23_nextup.h`, matching C23 7.12.11.5/7.12.11.6:

```c
float       nextupf(float x);
double      nextup(double x);
long double nextupl(long double x);
float       nextdownf(float x);
double      nextdown(double x);
long double nextdownl(long double x);
```

The header has an include guard and `extern "C"` linkage in C++ mode; it
compiles standalone under `-std=c99 -Wall -Wextra -pedantic` and from C++17.
On this SDK `<math.h>` declares none of these names (verified: no SDK header
match), so there is no redeclaration conflict.

## Host floating-point facts (measured, not assumed)

```
Apple clang version 16.0.0 (clang-1600.0.26.6)
sizeof(long double)=8 LDBL_MANT_DIG=53 DBL_MANT_DIG=53
```

`long double` is binary64 on this host (8 bytes, same as `double`), so the
binary64 expected bit patterns apply to `nextupl`/`nextdownl` too. The test
guards those checks on `sizeof(long double) == 8 && LDBL_MANT_DIG ==
DBL_MANT_DIG` rather than assuming it in general. All input values in the test
are stored as **integer bit fields** and converted with `memcpy`; storing a
bit pattern directly in a floating-point field would silently convert it as a
number (e.g. `0x8000000000000000ULL` → `2^63`).

## Expected bit patterns asserted (excerpt; full table in the test)

| Input | `nextup` bits | `nextdown` bits |
|-------|---------------|-----------------|
| `+0` (`0000000000000000`) | `0000000000000001` | `8000000000000001` |
| `-0` (`8000000000000000`) | `0000000000000001` | `8000000000000001` |
| `1.0` (`3ff0000000000000`) | `3ff0000000000001` | `3fefffffffffffff` |
| `-1.0` (`bff0000000000000`) | `bfefffffffffffff` | `bff0000000000001` |
| least ±subnormal | `0000000000000002` / `8000000000000000` | `0000000000000000` / `8000000000000002` |
| max subnormal / `DBL_MIN` | `0010000000000000` | `000fffffffffffff` |
| `+DBL_MAX` / `-DBL_MAX` | `7ff0000000000000` / `ffeffffffffffffe` | `7feffffffffffffe` / `fff0000000000000` |
| `+inf` / `-inf` | `7ff0000000000000` / `ffefffffffffffff` | `7fefffffffffffff` / `fff0000000000000` |
| quiet NaN (`7ff8000000000000`) | `7ff8000000000000` (unchanged) | `7ff8000000000000` (unchanged) |

Equivalent binary32 patterns (`float`: `00000001`/`80000001` from ±0,
`3f800001`/`3f7fffff` from 1.0, `7f800000`/`ff800000` at ±inf, `7fc00000`
NaN preserved) and the binary64 patterns for `long double` are asserted as
well: 13 input cases × 2 directions × 3 types = 78 checks, plus 6
`nextafter`-delegation checks on a non-anchor value = **84 checks**.

## Exact commands and results

All commands were run from the repository root. `TMP` is a per-session temp
directory (`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/c23_nextup`);
no build script was edited.

### 1. Compile the implementation and test with the mandated flags

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/math/c23_nextup.c -o "$TMP/c23_nextup.o"
# no output; exit=0

/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_c23_nextup.c "$TMP/c23_nextup.o" -lm -o "$TMP/test_c23_nextup"
# no output; exit=0
```

Both compile with **zero warnings** under `-Wall -Wextra -pedantic`.

### 2. Deterministic run under `timeout 30`

```sh
timeout 30 "$TMP/test_c23_nextup"
# c23_nextup: 84 0
# exit=0
```

Result: **PASS — 84 checks, 0 failures.**

### 3. Symbol inventory of the new object

```sh
nm -g "$TMP/c23_nextup.o"
#                  U _nextafter
#                  U _nextafterf
#                  U _nextafterl
# 00000000000000b0 T _nextdown
# 0000000000000084 T _nextdownf
# 00000000000000dc T _nextdownl
# 000000000000002c T _nextup
# 0000000000000000 T _nextupf
# 0000000000000058 T _nextupl
```

Exactly six defined globals (`T`), the three expected undefined libm
references (`U`). No other symbol is defined.

### 4. ABI evidence — new symbols only, nothing renamed or retyped

The project archive `build/libc/libcplus_hardened.a` (784 defined globals) was
copied to a temp file and the new object inserted there; the original archive
was not modified. Defined-global symbol sets were compared after filtering
out `nm`'s archive member headers:

```sh
BASE=build/libc/libcplus_hardened.a
nm -g --defined-only "$BASE" | grep -E '^[0-9a-fA-F]+ [A-Za-z] ' | sort > "$TMP/before.syms"
wc -l < "$TMP/before.syms"        # -> 784

cp "$BASE" "$TMP/libc_with_c23_nextup.a"
ar r "$TMP/libc_with_c23_nextup.a" "$TMP/c23_nextup.o"
nm -g --defined-only "$TMP/libc_with_c23_nextup.a" \
  | grep -E '^[0-9a-fA-F]+ [A-Za-z] ' | sort > "$TMP/after.syms"
wc -l < "$TMP/after.syms"         # -> 790

diff "$TMP/before.syms" "$TMP/after.syms"
# 68a69
# > 0000000000000000 T _nextupf
# 74a76
# > 000000000000002c T _nextup
# 116a119
# > 0000000000000058 T _nextupl
# 148a152
# > 0000000000000084 T _nextdownf
# 173a178
# > 00000000000000b0 T _nextdown
# 203a209
# > 00000000000000dc T _nextdownl

comm -23 "$TMP/before.syms" "$TMP/after.syms"
# (no output) -- no symbol was removed or retyped
```

Before/after nextup-family symbols, showing the pre-existing `cplus_*`
wrappers unchanged alongside the six additions:

```
0000000000000000 T _cplus_nextup
000000000000003c T _cplus_nextupf
0000000000000078 T _cplus_nextupl
00000000000000b4 T _cplus_nextdown
00000000000000f0 T _cplus_nextdownf
000000000000012c T _cplus_nextdownl
0000000000000000 T _nextupf        <- new
000000000000002c T _nextup         <- new
0000000000000058 T _nextupl        <- new
0000000000000084 T _nextdownf      <- new
00000000000000b0 T _nextdown       <- new
00000000000000dc T _nextdownl      <- new
```

**ABI conclusion:** purely additive — six new global symbols, zero changed or
removed exports.

### 5. UBSan run

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/math/c23_nextup.c -o "$TMP/c23_nextup_ubsan.o"
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_c23_nextup.c "$TMP/c23_nextup_ubsan.o" -lm \
  -o "$TMP/test_c23_nextup_ubsan"
UBSAN_OPTIONS=print_stacktrace=1 timeout 30 "$TMP/test_c23_nextup_ubsan"
# c23_nextup: 84 0
# exit=0; no UBSan diagnostics
```

### 6. Integration link against the archive copy (no separate object)

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_c23_nextup.c "$TMP/libc_with_c23_nextup.a" -lm \
  -o "$TMP/test_c23_nextup_archive"
timeout 30 "$TMP/test_c23_nextup_archive"
# c23_nextup: 84 0
# exit=0
```

This confirms the intended integration shape: once the parent agent adds
`c23_nextup.c` to the build source list, the test links straight from the
project archive.

### 7. Standalone header smoke test

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include -fsyntax-only "$TMP/hdr.c"     # exit=0
/usr/bin/clang++ -std=c++17 -Wall -Wextra -pedantic \
  -Ilibc/install-include -fsyntax-only "$TMP/hdr.cpp"   # exit=0
```

## Deviations / limitations (explicit, not hidden)

1. **Platform-backed, not a from-scratch C23 implementation.** The bodies are
   one-line delegations to the host libm `nextafter` family. They are
   bit-exact to the C23 special cases listed above on this host, but they
   inherit host behaviour rather than reimplementing the operation.
2. **Floating-point exception flags are host-defined.** `nextafter` raises
   `FE_OVERFLOW|FE_INEXACT` at `TYPE_MAX -> inf` and
   `FE_UNDERFLOW|FE_INEXACT` at subnormal steps, whereas a dedicated C23
   `nextup`/`nextdown` is recommended to raise no exception except for a
   signaling NaN (e.g. the glibc manual documents that property for its own
   `nextup`). No `fenv` masking is performed; this is recorded, not hidden.
3. **`long double` is binary64 on this host.** `sizeof(long double) == 8` and
   `LDBL_MANT_DIG == DBL_MANT_DIG == 53`, so `nextupl`/`nextdownl` are
   bit-identical to `nextup`/`nextdown` here. The test detects this instead of
   assuming it; on a platform with a wider `long double` those two checks are
   skipped and would need platform-specific expected patterns.
4. **Bare ISO names are intentional.** This differs from the project's usual
   `cplus_*` namespacing (the existing `cplus_nextup` wrappers remain
   untouched). No SDK header on this host declares the bare names. On a future
   toolchain that both declares and exports them, a compatible redeclaration
   is fine in C, but a duplicate definition at link time could require
   revisiting the namespacing decision.
5. **Build wiring is out of scope for this agent.** Per strict file ownership,
   no source list was edited; verification linked the new object explicitly
   or from an archive copy, as shown above.

**Remaining blocker for this task: none.**

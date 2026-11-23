# REDTEAM format_io — agent 4/10

Strict file ownership: every file below lives in `tests/redteam/format_io/`.
No source, header, contract, or archive was modified. No `git add`/commit was
performed. `scenarios.log` is gitignored locally; `digest.txt` is a committed
artifact containing the SHA-256 of the exact scenario log.

## 1. Target verification (`nm`)

From repo root:

```sh
nm -g --defined-only build/safe/libcplus_safecrt.a | grep -E 'sprintf_s|snprintf_s|printf_s|fprintf_s|sscanf_s|fscanf_s|cplus_fopen_s|fread_s|fwrite_s|fclose_s' | awk '{print $NF}' | sort -u
```

Verified defined symbols in `build/safe/libcplus_safecrt.a`:

```
_cplus_fclose_s          _cplus_fopen_s              _cplus_fprintf_s
_cplus_fprintf_s_checked _cplus_fread_s              _cplus_fscanf_s
_cplus_fscanf_s_checked  _cplus_printf_s             _cplus_printf_s_checked
_cplus_snprintf_s        _cplus_snprintf_s_checked   _cplus_sprintf_s
_cplus_sprintf_s_checked _cplus_sscanf_s             _cplus_sscanf_s_checked
_cplus_vsnprintf_s       _cplus_vsprintf_s
```

`build/libc/libcplus_hardened.a` defines **0** checked-`_s` symbols. Both
archives are linked because the safecrt adapters reference the hardened libc
support layer (matching the existing `tests/adversarial_1000/fmt_io` link line).

## 2. Harness

- **Fixed PRNG**: LCG state `0x52454454464D4931` ("REDTFIO1"), two draws per
  case; every one of the 6000 cases is byte-for-byte reproducible.
- **Forked children**: one `fork()` per case. Each child redirects `stdout`
  to `/dev/null`, disables the SafeCRT violation trap (so promised refusals
  return `-1`/`EINVAL`/`EOF` instead of `exit(70)`), runs one attack, and
  reports raw facts `{refused, canary_ok, rc, errno, wrote}` over a pipe.
  The parent classifies `REFUSED / CRASH / CORRUPT / UNEXPECTED_OK / OK` and
  appends one `C ...` line to `scenarios.log`. A child killed by a signal is
  `CRASH`; a changed canary is `CORRUPT`.
- **Runner**: `sh tests/redteam/format_io/run.sh`. Builds with
  `/usr/bin/clang -std=c99 -O1 -g`, links both archives, redirects harness
  stdout to `stdout_sink.txt`, writes `scenarios.log` + `summary.txt`, and
  computes `digest.txt` (SHA-256 of `scenarios.log`). Exit 0.
- **Attack classes exercised** (353 cases each, 17 kinds, 6000 total):
  direct `%n` variants; positional `%n` bypass (corrupt + crash forms);
  struct-member destination overflow; flat-array one-byte-too-small `dmax`;
  non-literal format; null format/stream/buffer; huge `%s` width; unbounded
  scanf `%s`/`%[...]`; safe scanf widths; double close; `fread_s`/`fwrite_s`
  bmax clamping; `SIZE_MAX` count; null I/O args; `../` path in `fopen_s`.

### Summary (canonical line)

```
REDTEAM format_io 6000 2824 353 706 0
```

`cases=6000, refused=2824, crashes=353, corrupt=706, unexpected_ok=0`.

Per-kind classification (from `scenarios.log`):

```
353 PCTN_DIRECT         REFUSED       353 PCTN_BYPASS        CORRUPT
353 PCTN_BYPASS_CRASH   CRASH         353 STRUCT_MEMBER      CORRUPT
353 FLAT_ONEOVER        REFUSED       353 NONLITERAL         REFUSED
353 NULL_FMT            REFUSED       353 NULL_STREAM        REFUSED
353 SCAN_UNBOUNDED      REFUSED       353 DOUBLE_CLOSE       REFUSED
353 IO_NULL             REFUSED       353 HUGE_WIDTH         OK
353 SCAN_SAFE           OK            353 FREAD_BOUNDS       OK
353 FWRITE_BOUNDS       OK            353 HUGE_COUNT         OK
352 FOPEN_TRAVERSAL     OK
```

Determinism: three consecutive full runs produced identical SHA-256 digests
(the value in `digest.txt`). All 6 `stderr.log` non-`[CPLUS-SAFE]` lines are
empty — no harness-side errors.

`scenarios.log` SHA-256: `see digest.txt`.

## 3. FINDING F1 — `%n` detector bypass via repeated positional specifiers

**Severity:** high. **Promise violated:** *"`%n` ... is refused in all format
and scan functions"* (`stdlib/safe/safe_format.h`), and `fmt_violation(...
"forbidden %n specifier in format string")` in `safe_format.c`.

**Reproducer** (`tests/redteam/format_io/repro_f1_pctn_bypass.c`):

```sh
/usr/bin/clang -std=c99 -O1 -w -Istdlib/safe -Ilibc/install-include \
  -Ithird_party/safeclib/include \
  tests/redteam/format_io/repro_f1_pctn_bypass.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/repro_f1 && /tmp/repro_f1
```

Observed (vulnerable):

```
ABCDfmt=ABCD%1$2$n rc=4 errno=0 sink=0x00000004 (0x24681357 == no write)
```

`rc >= 0`, `errno == 0`, and the canary `sink` was overwritten with `4` —
the number of bytes emitted before `%n`. No refusal happened.

**Mechanism.** In `cplus_fmt_has_percent_n()` (`safe_format.c`):

```c
q = p;
while (*q >= '0' && *q <= '9') ++q;
if (q > p && *q == '$') p = q + 1;          /* consumes "1$" */
...
if (*p == '*') { ... } else {
    while (*p >= '0' && *p <= '9') ++p;     /* consumes "2" of "2$" */
}
...
if (*p == 'n') return 1;                    /* p is at '$', not 'n' */
if (*p) ++p;                                /* steps past '$' to 'n' */
```

For `%1$2$n` the scanner consumes `1$` as a position, `2` as width digits,
then skips the remaining `$`, leaving the cursor on the final `n`. It never
recognises a `%n` conversion. libc parses `%1$2$n` as a real `%n` and writes
through the supplied pointer. The same bypass works for `%2$1$n`,
`%1$2$3$n`, `%1$2$hn`, `%1$-2$n`, `%01$02$n`, `%1$2$lln`, and with any
literal prefix (e.g. `ABCD%1$2$n` → `sink == 4`). When the positional index
exceeds the number of supplied arguments (`%11$22$n`), the missed check lets
libc read a wild vararg and the child dies with SIGSEGV — the harness records
353 such `CRASH` cases.

**Affected APIs:** `printf_s`, `fprintf_s`, `snprintf_s`, `sprintf_s` (all call
`cplus_fmt_has_percent_n`). `sscanf_s`/`fscanf_s` use a different scanner and
were not observed to write for this string.

**Note on scope.** Apple libc aborts on `%n` in a *heap/stack-allocated*
(non-read-only) format string via its own fortification. The reproducer and
the harness deliberately use read-only string literals (the normal case for
checked `*_s` APIs), which reach the SafeCRT checker and expose the bug. This
is not an artifact of the harness: a literal format such as `ABCD%1$2$n` is
exactly what the compile-time format-attribute check is meant to validate.

## 4. FINDING F2 — destination-capacity check bypass for struct members

**Severity:** medium/high. **Promise violated:** *"check `dest` against ...
`__builtin_object_size(dest, 0)` to detect overflows where `dmax` exceeds the
real buffer capacity"* and *"bounded variants ... never write past `dmax`
bytes [of the real buffer]"* (`safe_format.h` / `safe_format.c`).

**Reproducer** (`tests/redteam/format_io/repro_f2_struct_member.c`):

```sh
/usr/bin/clang -std=c99 -O1 -w -Istdlib/safe -Ilibc/install-include \
  -Ithird_party/safeclib/include \
  tests/redteam/format_io/repro_f2_struct_member.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/repro_f2 && /tmp/repro_f2
```

Observed (vulnerable):

```
object_size(s.a,0)=16 (should be 8, is 16)
rc=15 errno=0 a=[0123456789ABCDE] canary=[89ABCDE] corrupted=1
```

The 8-byte member `s.a` received 16 bytes (15 chars + NUL); the adjacent
`canary[8]` field was overwritten. No refusal, `errno == 0`.

**Mechanism.** The macro

```c
#define snprintf_s(...) cplus_snprintf_s_checked(..., CPLUS_SAFE_OBJ_SIZE(CPLUS_SAFE_FMT_1ST(__VA_ARGS__)), __VA_ARGS__)
#define CPLUS_SAFE_OBJ_SIZE(p) __builtin_object_size((p), 0)
```

uses `__builtin_object_size(p, 0)`, which for a struct member returns the size
of the **enclosing object** (verified: 16 for `s.a` inside `struct {char a[8];
char can[8];}`). `fmt_check_dest_bounds()` then sees `cap = 16`, `dmax = 16`
and accepts the write. Type-1 object size would have returned 8 and refused.
The overflow is into an adjacent *member of the same object*, not an unknown
heap extent, so it is not covered by the documented "unknown extents" limit.

**Affected APIs:** `snprintf_s` and `sprintf_s` macros (and their `_checked`
forms) when the destination is a struct/union member whose enclosing object is
larger than the member. The harness records 353 such `CORRUPT` cases.

## 5. Classes that did NOT yield findings (honest negatives)

- Direct `%n` spellings (`%n`, `%hhn`, `%lln`, `%zn`, `%1$n`, `AB%n`, `%*n`,
  `%5.3n`) — all `REFUSED`, canary intact.
- Non-read-only (heap/stack) format strings with `%n`: Apple libc aborts before
  the write (defense in depth outside this library) — not counted as a library
  finding.
- Non-literal format via `*_checked` with `fmt_is_literal == 0` — `REFUSED`.
- Null `fmt` / `stream` / `buf` — `REFUSED` (`-1`/`EOF`/`0`, `EINVAL`).
- Flat-array `dmax = sizeof(buf) + 1` — `REFUSED`, canary intact.
- Unbounded scanf `%s` / `%[a-z]` — `REFUSED`; bounded `%7s` / `%7[a-z]` — OK.
- Double `fclose_s` — second call `REFUSED` (`EOF`/`EINVAL`), no UAF.
- `fread_s`/`fwrite_s` with `bmax < size*count`, `SIZE_MAX` count — clamped to
  `bmax/size`, canary intact; null buffer/stream — `REFUSED`.
- `fopen_s` with a `sub/../payload.txt` traversal path — opened successfully.
  `fopen_s` does not promise path confinement, so this is **not** a finding;
  it is recorded as `OK` coverage only.

## 6. Scope limits

These 6000 deterministic cases are evidence, not proof. They exercise the
named checked functions under this toolchain (Apple clang 16, arm64
macOS) and build configuration (`-O1`). They do not cover every input,
thread interleaving, or live-game/Wine scenario. No live-game injection was
performed. The two findings above are genuine because refusals are explicitly
promised and the observed `CORRUPT`/`CRASH` outcomes violate them.

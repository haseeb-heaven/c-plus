# Blue-team agent 9 — checked format & I/O hardening

Scope (strict): `stdlib/safe/safe_format.h`, `stdlib/safe/src/safe_format.c`,
`stdlib/safe/safe_io.h`, `stdlib/safe/src/safe_io.c`, `tests/defense/format_io/`.
No redteam file was edited; no `git add`/commit was performed. The redteam
`digest.txt` that `run.sh` regenerates was restored from git afterwards.

## ABI rationale

Only static helpers (`cplus_fmt_has_positional`, `fmt_check_stream`) and macro
logic changed. `nm -g --defined-only build/safe/libcplus_safecrt.a` still exports
exactly the same 18 checked symbols (`cplus_{snprintf,sprintf,printf,fprintf,
scanf,fscanf,sscanf}_s[_checked]`, `cplus_{fopen,fclose,fread,fwrite}_s`,
`cplus_{v snprintf,v sprintf}_s`). No new exported symbol, no signature change,
no struct/layout change. `CPLUS_SAFE_OBJ_SIZE` is a macro default only.

---

## F1 (HIGH) — `%n` detector bypass via repeated positional specifiers

**Gap.** `cplus_fmt_has_percent_n()` consumed `N$` as a positional index, then
`M$`'s digits as width, skipped the stray `$`, and landed *after* the `n` of
`"%1$2$n"`. libc parsed it as a real `%n` write. `"%11$22$n"` read a wild
vararg and SIGSEGV'd. Affected `printf_s`, `fprintf_s`, `snprintf_s`,
`sprintf_s` (the printf scanner); the scanf scanner had the same structural
weakness.

**Fix.** New static helper `cplus_fmt_has_positional()` refuses any format
where `$` appears immediately after `%` or after the decimal argument index —
i.e. a real positional marker. A literal `$` in ordinary format text is left
alone, so non-positional behavior is unchanged. Called from both
`cplus_fmt_has_percent_n()` and `cplus_scanf_fmt_check()`, so the whole class of
positional-specifier bypasses is closed, not just the observed spellings.

### Exact commands and before/after

Build the archive and both reproducers verbatim as documented by redteam:

```sh
CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh
/usr/bin/clang -std=c99 -O1 -w -Istdlib/safe -Ilibc/install-include \
  -Ithird_party/safeclib/include \
  tests/redteam/format_io/repro_f1_pctn_bypass.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/repro_f1 && /tmp/repro_f1
```

| | result |
|---|---|
| **BEFORE** | `ABCDfmt=ABCD%1$2$n rc=4 errno=0 sink=0x00000004 (0x24681357 == no write)` — real `%n` write, no refusal |
| **AFTER** | `[CPLUS-SAFE] printf_s: forbidden %n specifier in format string` / `fmt=ABCD%1$2$n rc=-1 errno=22 sink=0x24681357` — **REFUSED**, no write, no crash |

Control (same build): `snprintf_s(buf, n, "price: $5")` returns 9 and
`snprintf_s(buf, n, "100%%%%n ok")` returns 9 — literal `$` and `%%` still work.

---

## F2 (MEDIUM/HIGH) — struct-member destination-capacity bypass

**Gap.** The unprefixed `snprintf_s`/`sprintf_s` macros passed
`__builtin_object_size(dest, 0)` as `known_dmax`. For `s.a[8]` inside a
16-byte struct, type 0 returned the *enclosing struct* size (16), so
`snprintf_s(s.a, 16, ...)` passed the capacity check and overwrote `s.can[8]`.

**Fix.** `CPLUS_SAFE_OBJ_SIZE(p)` now uses `__builtin_object_size(p, 1)`
(nearest enclosing object = the member subobject). For flat arrays and whole
objects type 0 and type 1 are identical, so valid behavior is unchanged. Heap
buffers are still covered by the `malloc_s` tracked-size check in
`fmt_check_dest_bounds()` (both types return `-1` there).

### Exact commands and before/after

```sh
/usr/bin/clang -std=c99 -O1 -g -w -Istdlib/safe -Ilibc/install-include \
  -Ithird_party/safeclib/include \
  tests/redteam/format_io/repro_f2_struct_member.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o /tmp/repro_f2 && /tmp/repro_f2
```

| | result |
|---|---|
| **BEFORE** | `object_size(s.a,0)=16` / `rc=15 errno=0 a=[0123456789ABCDE] canary=[89ABCDE] corrupted=1` — adjacent field overwritten |
| **AFTER** | `[CPLUS-SAFE] snprintf_s: format dmax exceeds destination buffer capacity` / `rc=-1 errno=22 ... canary=[ZZZZZZZZW] corrupted=0` — **REFUSED**, canary intact |

Note: the reproducer prints `s.a`/`s.can` with `%s` although they are not
NUL-terminated, so the trailing `W` is an adjacent stack byte from an unrelated
read past the end — not corruption by our function. `corrupted=0` is authoritative.

---

## Redteam harness (`sh tests/redteam/format_io/run.sh`)

Fixed seed `0x52454454464D4931`, 6000 forked cases. Canonical summary line:

```
BEFORE:  REDTEAM format_io 6000 2824 353 706 0
AFTER:   REDTEAM format_io 6000 3177   0   0 706
         ( cases, refused, crashes, corrupt, unexpected_ok )
```

| metric | before | after |
|---|---|---|
| crashes | 353 | **0** |
| corrupt | 706 | **0** |
| refused | 2824 | 3177 |
| unexpected_ok | 0 | 706 |

The 706 `UNEXPECTED_OK` entries are **not** residual findings: all 706 have
`rc=-1 err=22 canary=1` (correct refusals). They are classified
`UNEXPECTED_OK` only because the harness's own `child_run()` never sets its
`refused` flag for the `PCTN_BYPASS_CRASH` and `STRUCT_MEMBER` kinds (353 each);
those two kinds previously showed as CRASH/CORRUPT and now show as refused
facts with the harness's flag still 0. No memory corruption and no crash
remain. Per-kind after: `PCTN_BYPASS 353 REFUSED`, `STRUCT_MEMBER 353
UNEXPECTED_OK (rc=-1/err=22)`, `PCTN_BYPASS_CRASH 353 UNEXPECTED_OK
(rc=-1/err=22)`, all other 17 kinds unchanged.

---

## Earlier defense-in-depth (same agent, already landed)

Measured against true baseline `5fe387d` sources compiled into a parallel tree
(`/tmp/baseline_tree`), each test under `timeout 30`:

| gap | baseline | hardened |
|---|---|---|
| `fprintf_s`/`fscanf_s` on a stream closed by `cplus_fclose_s` (UAF) | reached host `vfprintf`/`vfscanf`: `fprintf on closed stream returned -1`, exit 0, no trap | new static `fmt_check_stream()` → violation; child exit 70, recoverable mode `-1`/EINVAL |
| `fread_s`/`fwrite_s` on closed stream | host `fread` called on freed `FILE*`; errno=9 (EBADF), no EINVAL refusal | new `io_check_stream()` → `0`/EINVAL, no host call |
| `fread_s`/`fwrite_s` with `bmax` > real tracked allocation | `bmax=16` on an 8-byte `malloc_s` block: `n=16`, buffer bytes `30 31 32 33 34 35 36 37` (overflow) | new `io_check_buf()` → `0`/EINVAL, bytes untouched |
| `fread_s`/`fwrite_s` with freed buffer | host call on freed pointer | refused via `cplus_safe_ptr_was_freed` |
| `fopen_s` with freed `name`/`mode` | host `fopen` on freed pointer | refused via `cplus_safe_ptr_was_freed` |

Concrete baseline/hardened probe (`claimed bmax=16, actual alloc=8`):
both showed `n=16` for host `fread`, but only the hardened wrapper refused the
call before reaching the host; the regression test asserts `n==0`, `errno==EINVAL`
and untouched guard bytes.

Defense test binaries (baseline `5fe387d` → hardened):

```
BEFORE:  test_format_hardening: FAILURES=4   test_io_hardening: FAILURES=7
AFTER:   test_format_hardening: ok           test_io_hardening: ok
```

Gaps audited and found **already sound** (no change needed): `%n` rejection in
ordinary length-modified forms (`%hhn`, `%lln`, `%zn`, positional `%1$n`),
non-literal format strings (all six `_checked` macros), one-byte-too-small
`dmax` on flat arrays, NUL termination / would-be return, null dest/format/
stream, huge `count` clamp (`count > bmax/size`), bmax clamping, and the
tombstone/compaction double-close cache. Evidence: `tests/crt/unit/
test_format_security_s.c`, `test_sprintf_truncation_contract.c`,
`test_fclose_s.c`, `tests/crt/adversarial/test_io50_clamp_bmax.c`.

Documented residual limit (not a gap fixable without ABI/source-compat change):
the raw `cplus_*_s` symbols cannot check literal-ness at runtime (no flag
parameter); enforcement lives in the unprefixed macros and the compiler pass.
Stack/static I/O buffers cannot be cross-checked inside the wrapper because
`__builtin_object_size` must be evaluated at the call site; they remain bounded
by the `count <= bmax/size` clamp.

---

## Verification commands (all under `timeout 30`, all passing)

```sh
# rebuild archive with /usr/bin/clang (exact command from parent)
CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh

# blue-team regressions (rebuilds archive itself)
sh tests/defense/format_io/run.sh
#   test_f1_f2_regression: ok / PASS
#   test_format_hardening: ok / PASS
#   test_io_hardening: ok / PASS
#   exit 0

# redteam harness (canonical before/after above)
sh tests/redteam/format_io/run.sh
#   REDTEAM format_io 6000 3177 0 0 706

# existing CRT regressions (all rc=0)
for t in test_format_security_s test_sprintf_truncation_contract \
         test_snprintf_s test_sprintf_s test_printf_s test_fprintf_s \
         test_fscanf_s test_vsnprintf_s test_annex_k_closure \
         test_all_functions_10_io_format; do
  /usr/bin/clang -w -std=c11 -Istdlib/safe -Ithird_party/safeclib/include \
    -Ithird_party/exceptions4c tests/crt/unit/$t.c \
    build/safe/libcplus_safecrt.a -lpthread -lm -o /tmp/rt_$t
  timeout 30 /tmp/rt_$t
done

# ABI check
nm -g --defined-only build/safe/libcplus_safecrt.a | \
  grep -E 'cplus_(snprintf|sprintf|printf|fprintf|scanf|fscanf|sscanf|fopen|fclose|fread|fwrite)' | \
  awk '{print $NF}' | sort -u    # 18 symbols, identical to baseline
```

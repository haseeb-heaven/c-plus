# AUDIT — stdio and formatting wrappers (branch `agent/modern-stdlib`)

**Scope (read-only audit):** every exported stdio / formatting wrapper implemented in
`libc/src/stdio/{stdio.c,stdio2.c,stdio3.c}`, `stdlib/safe/src/{safe_io.c,safe_format.c}`,
with public contracts in `stdlib/safe/{safe_io.h,safe_format.h}` and
`libc/install-include/cplus_libc.h`.

**Author role:** parallel audit agent #5. No source files were modified. Evidence probes
were compiled from stdin into the pre-created OpenCode temp directory only; no
repository file other than this document was created or changed.

**Bottom line:** the raw/`_s` wrappers have a coherent *documented* threat model — raw
C pointers stay caller-owned — but the two "double-close protection" caches contain
**demonstrable use-after-free / fd-leak defects**, and the checked-format literal gate
is defense-in-depth only (bypassable by direct `cplus_*_s`/`v*` calls and by compiler
literal-detection limits).

---

## 1. Severity scale and reference IDs

| Severity | Meaning |
|---|---|
| **HIGH** | Reproducible memory-safety violation (UAF/double-close), resource leak on ordinary use, or guard that can be defeated with realistic inputs. |
| **MED** | Crash/UB on plausible caller error, contract deviation with security impact, or a security gate with a known bypass/limitation. |
| **LOW** | Documented limitation, ergonomic/contract mismatch, or failure-path cleanup gap with limited blast radius. |
| **INFO** | Working as documented; recorded for completeness. |

Check IDs used in the tables:

| ID | Check |
|---|---|
| `NULL-fmt` | NULL format-string rejection |
| `NULL-str` | NULL stream rejection |
| `NULL-buf` | NULL destination/buffer rejection (usually `size>0` conditioned) |
| `OVERFLOW` | `size * nmemb` overflow guard, `errno = EOVERFLOW` |
| `BMAX` | `count = bmax / size` clamp (checked I/O only) |
| `DBL-close` | recently-closed-address / live-stream table lookup |
| `REGISTER` | stream added to registry on create/reopen |
| `%n` | runtime `%n` rejection (`cplus_fmt_has_percent_n`) |
| `LITERAL` | runtime non-literal-format refusal via `cplus_*_s_checked` macro path (`__builtin_constant_p`) |
| `SCAN` | `%n` + unbounded `%s`/`%[` refusal (`cplus_scanf_fmt_check`) |
| `UAF-ptr` | freed-pointer detection for `fmt`/`dest`/`str` (`cplus_safe_ptr_was_freed`) |
| `CAP` | `dmax` vs tracked/`__builtin_object_size` capacity (`fmt_check_dest_bounds`) |
| `ATTR` | compile-time `format(printf|scanf,…)` attribute |
| `LOG` | metadata-only trace/stat recording (not a safety control) |

Unsafe-case IDs (all "NOT handled"):

| ID | Unhandled unsafe case |
|---|---|
| `U-FILE` | invalid / freed / foreign `FILE*` (no stream liveness validation in any I/O call) |
| `U-RAWFMT` | raw format misuse (`%n`, attacker-controlled format) in the plain wrappers |
| `U-PARTIAL` | partial reads/writes: short item count returned but no dedicated signal/retry |
| `U-BUF` | `fflush(stdin)` UB and update-stream read/write sequencing rules not enforced |
| `U-TMP` | `tmpfile`/temp-file cleanup on `fmt_violation` → `_exit(70)` not guaranteed |
| `U-VARI` | variadic type/count mismatch is UB at runtime (no runtime checks possible) |
| `U-BMAX` | `bmax`/`dmax` trusted; not tied to real buffer capacity in checked I/O |
| `U-LIT` | literal gate false-negatives (direct/`v*` calls) and false-positives (const arrays) |
| `U-REUSE` | recycled `FILE*` addresses and bounded/incomplete caches |
| `U-NUL` | truncation and NUL-termination obligations left to caller |

Backend legend: **host** = delegated directly to installed system libc; **host+reg** =
delegated plus stream-registry side effect; **host+gate** = delegated after runtime
checks; **checked** = SafeCRT wrapper with logging/violation handling.

---

## 2. Plain libc stdio — `libc/src/stdio/stdio.c`

All format attributes are declared in `cplus_libc.h:120-132`. None of these functions
reject `%n` or non-literal formats; that is intentional for the ANSI layer (see §6.3).

| Function | Signature | Backend | Checks present | Unsafe cases NOT handled | Severity | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `cplus_fopen` | `FILE *fopen(const char*, const char*)` | host+reg | `NULL-fmt`-style path/mode NULL (`errno=EINVAL`), `REGISTER` | `U-FILE` (caller must use returned handle), mode string not validated beyond host | INFO | `stdio.c:54-60` | Document that only wrapped creators register. |
| `cplus_fclose` | `int fclose(FILE*)` | host+reg+DBL | `NULL-str` (`EOF`/`EINVAL`), `DBL-close` (256-entry closed ring + 256 live) | `U-REUSE` (raw `fopen`/`popen`/`fmemopen` at recycled address falsely rejected → **fd leak**; ring wraps after 256), `U-FILE` | **HIGH** | `stdio.c:10-37,62-82`; repro §8.1 | Replace address cache with generation-tagged liveness; unmark on every creation path; share with safe registry. |
| `cplus_fread` | `size_t fread(void*,size_t,size_t,FILE*)` | host | `NULL-str`, `NULL-buf` (only when `size>0&&nmemb>0`), `OVERFLOW` → `EOVERFLOW` | `U-FILE`, `U-PARTIAL`, `U-BUF` | INFO | `stdio.c:84-89`; `stdio_attack_NOTES.md` §1 | Correct as written; keep `EOVERFLOW`. |
| `cplus_fwrite` | `size_t fwrite(const void*,size_t,size_t,FILE*)` | host | same as `fread` | `U-FILE`, `U-PARTIAL`, `U-BUF` | INFO | `stdio.c:91-96` | Correct as written. |
| `cplus_snprintf` | `int snprintf(char*,size_t,const char*,…)` | host | `NULL-fmt`, `NULL-buf` (size>0), `ATTR` | `U-RAWFMT`, `U-VARI`, `U-NUL` | INFO | `stdio.c:104-108`; `cplus_libc.h:127` | Callers wanting gates must use `snprintf_s` macro. |
| `cplus_vsnprintf` | `int vsnprintf(char*,size_t,const char*,va_list)` | host | `NULL-fmt`, `NULL-buf`, `ATTR` | `U-RAWFMT`, `U-LIT` (no literal gate), `U-VARI`, `U-NUL` | INFO | `stdio.c:98-102` | Same. |
| `cplus_printf` / `cplus_vprintf` | `int printf(const char*,…)` / `int vprintf(const char*,va_list)` | host | `NULL-fmt`, `ATTR` | `U-RAWFMT`, `U-LIT`, `U-VARI` | INFO | `stdio.c:110-114,164-168` | Use `printf_s` macro for CWE-134 gate. |
| `cplus_fprintf` / `cplus_vfprintf` | `int fprintf(FILE*,const char*,…)` / `int vfprintf(FILE*,const char*,va_list)` | host | `NULL-str`, `NULL-fmt`, `ATTR` | `U-FILE`, `U-RAWFMT`, `U-VARI`, `U-PARTIAL` | INFO | `stdio.c:116-120,158-162` | Use `fprintf_s` macro. |
| `cplus_scanf` / `cplus_fscanf` / `cplus_vfscanf` / `cplus_vscanf` | `int scanf(const char*,…)` etc. | host | `NULL-str` (f/vf variants), `NULL-fmt`, `ATTR` | `U-RAWFMT`, `U-VARI`, unbounded `%s`/`%[` | INFO | `stdio.c:122-132,170-174`; `math_gaps.c:32` (`vscanf`) | Use `*_scanf_s` when width enforcement is required. |
| `cplus_sprintf` / `cplus_vsprintf` | `int sprintf(char*,const char*,…)` / `int vsprintf(char*,const char*,va_list)` | host | `NULL-buf`, `NULL-fmt`, `ATTR` | **`U-BMAX` (unbounded write — no capacity parameter at all)**, `U-RAWFMT`, `U-VARI`, `U-NUL` | **MED** | `stdio.c:134-144` | Prefer `snprintf`; document `sprintf` as inherently unsafe. |
| `cplus_sscanf` / `cplus_vsscanf` | `int sscanf(const char*,const char*,…)` / `int vsscanf(const char*,const char*,va_list)` | host | `NULL-buf`, `NULL-fmt`, `ATTR` | `U-RAWFMT`, `U-VARI`, unbounded `%s`/`%[` | INFO | `stdio.c:146-156` | Use `sscanf_s` macro. |

## 3. Plain libc stdio — `stdio2.c` (unvalidated delegates)

Every function in this file delegates directly to the host with **no NULL/argument
checks, no registry interaction and no format attributes**. All have severity **MED**
(crash/UB on NULL) except where noted. Evidence: `stdio2.c:3-19`, declarations
`cplus_libc.h:133-139`.

| Function | Signature | Checks present | Unsafe cases NOT handled | Recommendation |
|---|---|---|---|---|
| `cplus_fseek` | `int fseek(FILE*,long,int)` | none (`LOG` only) | `U-FILE`, `U-BUF` (update-stream sequencing), NULL UB | Validate `stream`; consider wrapper that inserts mandated `fflush`/positioning. |
| `cplus_ftell` | `long ftell(FILE*)` | none | `U-FILE`, NULL UB | Validate `stream`. |
| `cplus_fflush` | `int fflush(FILE*)` | none | `U-BUF` — **`fflush(stdin)` is UB/undefined by ISO C**, no guard; NULL flushes all streams by host semantics | Explicitly document/define `fflush` on input streams; add update-stream helper. |
| `cplus_remove` | `int remove(const char*)` | none | NULL UB | Validate path. |
| `cplus_rename` | `int rename(const char*,const char*)` | none | NULL UB | Validate both paths. |
| `cplus_fgets` | `char *fgets(char*,int,FILE*)` | none | `U-FILE`, NULL/`n<=0` UB, `U-BUF` | Validate `s`, `n>0`, `stream`. |
| `cplus_fputs` | `int fputs(const char*,FILE*)` | none | `U-FILE`, NULL UB | Validate both. |
| `cplus_getc` / `cplus_putc` | `int getc(FILE*)` / `int putc(int,FILE*)` | none | `U-FILE`, NULL UB | Validate stream (contrast guarded `fgetc`/`fputc` in `stdio3.c`). |
| `cplus_feof` / `cplus_ferror` / `cplus_clearerr` | `int feof(FILE*)` etc. | none | `U-FILE`, NULL UB | Validate stream. |
| `cplus_putchar` / `cplus_getchar` / `cplus_puts` / `cplus_perror` | various | none | NULL UB for `puts`/`perror` | Document as raw delegates. |

Severity note: these are explicitly labeled "PLATFORM_BACKED … raw pointer validity …
caller's responsibility" in `cplus_libc.h:216-225`, so this is a **documented gap**, not a
hidden one; severity MED reflects crash-on-NULL, not memory corruption.

## 4. Plain libc stdio — `stdio3.c` (position/stream extras)

| Function | Signature | Backend | Checks present | Unsafe cases NOT handled | Severity | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `cplus_setvbuf` / `cplus_setbuf` | `int setvbuf(FILE*,char*,int,size_t)` / `void setbuf(FILE*,char*)` | host | none | `U-FILE`, `U-BUF`, NULL UB; `buf` lifetime/capacity caller-owned | MED | `stdio3.c:11-12` | Validate stream/mode; document buffer lifetime. |
| `cplus_fgetpos` / `cplus_fsetpos` | `int fgetpos(FILE*,fpos_t*)` / `int fsetpos(FILE*,const fpos_t*)` | host | none | `U-FILE`, NULL UB | MED | `stdio3.c:13-14` | Validate both args. |
| `cplus_ungetc` | `int ungetc(int,FILE*)` | host | none | `U-FILE`, NULL UB | MED | `stdio3.c:15` | Validate stream. |
| `cplus_fileno` | `int fileno(FILE*)` | host | none | `U-FILE`, NULL UB | MED | `stdio3.c:16` | Validate stream. |
| `cplus_fdopen` | `FILE *fdopen(int,const char*)` | host+reg | `REGISTER` | `U-FILE` (closed/invalid fd), mode not validated; **fd ownership after failure unclear** | LOW | `stdio3.c:17-22` | Document fd ownership; validate mode. |
| `cplus_freopen` | `FILE *freopen(const char*,const char*,FILE*)` | host+reg | `NULL-fmt`-style path/mode, `REGISTER` on success | `U-FILE` — **`stream` itself not NULL-checked**; on success with a *different* pointer the old address is not removed from the live table nor marked closed (stale registry entry) | MED | `stdio3.c:23-29` | Validate `stream`; reconcile old pointer on success/failure. |
| `cplus_tmpfile` | `FILE *tmpfile(void)` | host+reg | `REGISTER` (explicit fix for stale-address leak) | `U-TMP` (no automatic cleanup on `_exit(70)`), `U-REUSE` for non-wrapped creators | LOW | `stdio3.c:30-38` | Register/unmark via shared registry; register atexit cleanup. |
| `cplus_rewind` | `void rewind(FILE*)` | host | `NULL-str` | `U-FILE`, `U-BUF` | INFO | `stdio3.c:39-43` | OK. |
| `cplus_fgetc` | `int fgetc(FILE*)` | host | `NULL-str` | `U-FILE` | INFO | `stdio3.c:44-48` | OK. |
| `cplus_fputc` | `int fputc(int,FILE*)` | host | `NULL-str` | `U-FILE` | INFO | `stdio3.c:49-53` | OK. |

Wide-character format/stream wrappers (`cplus_fgetws`, `cplus_fputws`,
`cplus_wprintf`…`cplus_swscanf`, `cplus_fputwc`…`cplus_ungetwc`;
`cplus_libc.h:207-208,250-269`) are pure host delegates with **no checks and no
format attributes** (Apple Clang does not implement `format(wprintf,…)`). Severity
**MED** for the wide format family (`U-RAWFMT`, `U-VARI`, unbounded wide `%s`); caller
must supply literal formats and correct arguments. Evidence: `cplus_libc.h:223-224`
explicitly documents this.

## 5. Checked I/O — `stdlib/safe/src/safe_io.c`

| Function | Signature | Backend | Checks present | Unsafe cases NOT handled | Severity | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `cplus_fopen_s` | `FILE *fopen_s(const char*,const char*)` | host+reg | `NULL-buf` (name/mode → `EINVAL`), `REGISTER` + unmark from closed table, `LOG`, stat | `U-FILE`; **non-Annex-K signature** (`FILE*` return, no `FILE**` out-param / `errno_t`) | LOW | `safe_io.c:56-76`; `safe_io.h:10` | Document signature divergence; share registry with plain layer. |
| `cplus_fclose_s` | `int fclose_s(FILE*)` | host+reg+DBL | `NULL-str`, `DBL-close` (512-slot open-addressed live/closed caches), `LOG`, stat | **`U-REUSE` — probe-chain deletion without tombstones + `was_live` bypass defeats the guard → second `fclose` on freed `FILE*` (confirmed UAF).** Also table-full overwrite evicts markers; separate registry from `cplus_fclose`. | **HIGH** | `safe_io.c:13-54,78-108`; repro §8.2 | Tombstone/robin-hood deletion or generation tokens; refuse whenever the pointer is in *either* table; unify registries. |
| `cplus_fread_s` | `size_t fread_s(void*,size_t bmax,size_t size,size_t count,FILE*)` | host+BMAX | `NULL-buf`/`NULL-str`/`size!=0`/`count!=0`, `BMAX`, `LOG`, stat | `U-BMAX` (**`bmax` never validated against `__builtin_object_size`/tracked size**); silent clamp to 0 when `size>bmax`; no `errno` on invalid args; mislabels valid `size==0`/`count==0` as "invalid args" | **MED** | `safe_io.c:110-123` | Add `known_bmax` parameter (as format wrappers do) and refuse `dmax>cap`; set `EINVAL`. |
| `cplus_fwrite_s` | `size_t fwrite_s(const void*,size_t bmax,size_t size,size_t count,FILE*)` | host+BMAX | same as `fread_s` | same as `fread_s`, plus `U-PARTIAL` (short write returns count, no dedicated error) and no stream-mode check | **MED** | `safe_io.c:125-138` | Same + surface short writes via ferror/errno. |

## 6. Checked formatting — `stdlib/safe/src/safe_format.c`

### 6.1 Function-by-function

| Function | Signature | Checks present | Unsafe cases NOT handled | Severity | Evidence |
|---|---|---|---|---|---|
| `cplus_vsnprintf_s` | `int vsnprintf_s(char*,size_t,const char*,va_list)` | `NULL-fmt`, `UAF-ptr(fmt)`, `NULL-buf`, `CAP`, `%n`, `ATTR`, `LOG` | `U-LIT` (no literal gate on `v*`), `U-VARI`, `U-NUL` | **MED** | `safe_format.c:136-149` |
| `cplus_snprintf_s` | `int snprintf_s(char*,size_t,const char*,…)` | delegates to `vsnprintf_s` (all above), `ATTR` | `U-LIT` (direct call skips macro gate), `U-VARI`, `U-NUL` | **MED** | `safe_format.c:150-157` |
| `cplus_snprintf_s_checked` | `int (…, int fmt_is_literal, size_t known_dmax, char*,size_t,const char*,…)` | `NULL-fmt`, `UAF-ptr`, `LITERAL`, `NULL-buf`, `CAP(known_dmax)`, `%n`, `ATTR` | `U-LIT` (compiler-dependent literal detection), `U-VARI`, `U-NUL` | **MED** | `safe_format.c:158-172` |
| `cplus_printf_s` | `int printf_s(const char*,…)` | `NULL-fmt`, `UAF-ptr`, `%n`, `ATTR` | `U-LIT` (direct call skips macro), `U-VARI` | **MED** | `safe_format.c:173-186` |
| `cplus_printf_s_checked` | `int (int fmt_is_literal, const char*,…)` | above + `LITERAL` | `U-LIT`, `U-VARI` | **MED** | `safe_format.c:187-201` |
| `cplus_fprintf_s` | `int fprintf_s(FILE*,const char*,…)` | `NULL-str`, `NULL-fmt`, `UAF-ptr`, `%n`, `ATTR` | `U-FILE`, `U-LIT`, `U-VARI`, `U-PARTIAL` | **MED** | `safe_format.c:202-216` |
| `cplus_fprintf_s_checked` | `int (int fmt_is_literal, FILE*,const char*,…)` | above + `LITERAL` | `U-FILE`, `U-LIT`, `U-VARI` | **MED** | `safe_format.c:217-232` |
| `cplus_scanf_s` | `int scanf_s(const char*,…)` | `NULL-fmt`, `UAF-ptr`, `SCAN`, `ATTR` | `U-VARI`; **no Annex K `rsize_t` argument for `%s`/`%[`** — width must be literal in format; `U-LIT` | **MED** | `safe_format.c:233-246` |
| `cplus_scanf_s_checked` | `int (int fmt_is_literal, const char*,…)` | above + `LITERAL` | `U-LIT`, `U-VARI`, contract divergence | **MED** | `safe_format.c:247-261` |
| `cplus_fscanf_s` / `_checked` | `int fscanf_s(FILE*,const char*,…)` | `NULL-str`, `NULL-fmt`, `UAF-ptr`, `SCAN`, `ATTR` (+`LITERAL` in checked) | `U-FILE`, `U-LIT`, `U-VARI`, contract divergence | **MED** | `safe_format.c:262-292` |
| `cplus_sscanf_s` / `_checked` | `int sscanf_s(const char *str,const char*,…)` | `NULL-str`, `NULL-fmt`, `UAF-ptr(str,fmt)`, `SCAN`, `ATTR` (+`LITERAL`) | `U-LIT`, `U-VARI`, contract divergence; `str` NUL-termination caller-owned | **MED** | `safe_format.c:293-323` |
| `cplus_vsprintf_s` | `int vsprintf_s(char*,size_t,const char*,va_list)` | alias of `vsnprintf_s` (all checks) | `U-LIT`, `U-VARI`, `U-NUL`; **not Annex K semantics** (documented) | LOW | `safe_format.c:324-330` |
| `cplus_sprintf_s` | `int sprintf_s(char*,size_t,const char*,…)` | alias of bounded `vsprintf_s`, `ATTR` | same as `vsprintf_s`; `U-LIT` on direct call | LOW | `safe_format.c:331-338` |
| `cplus_sprintf_s_checked` | `int (int fmt_is_literal, size_t known_dmax, char*,size_t,const char*,…)` | `NULL-fmt`, `UAF-ptr`, `LITERAL`, `NULL-buf`, `CAP`, `%n`, `ATTR` | `U-LIT`, `U-VARI`, `U-NUL` | **MED** | `safe_format.c:339-353` |

All checked format functions call `fmt_violation()` → `cplus_runtime_violation()`, which
prints to stderr, `fflush(NULL)`, and `_exit(70)` when the violation trap is enabled
(`safe_runtime.c:49-61`). Default `g_violation_trap = 1`, so a *false positive* in the
literal/width gate terminates the process.

### 6.2 Gate implementation details

- `%n` parser `cplus_fmt_has_percent_n` (`safe_format.c:37-73`) walks flags, positional
  `$`, width/`*`, precision, and length modifiers `h l j z t L q`. It correctly catches
  `%n`, `%1$n`, `%1$*2$n`, `%hhn`, `%lln`, `%zn` and does not false-positive on `%%n`.
  It does **not** recognize non-standard length prefixes (e.g. MS `I64`), so a literal
  `%I64n` would slip through on a toolchain that supports it. LOW.
- `cplus_scanf_fmt_check` (`safe_format.c:75-112`) requires an explicit nonzero width or
  `*` suppression for `%s` and `%[…]`, and rejects `%n`. It does not handle glibc's `m`
  assignment-allocation modifier and does not consume an Annex K `rsize_t`. MED
  (contract divergence; a correct Annex-K call is rejected).
- `fmt_check_dest_bounds` (`safe_format.c:114-127`) prefers tracked `malloc_s` size, else
  the macro-supplied `CPLUS_SAFE_OBJ_SIZE(dest)`; unknown (`-1`) means no cap check.
  Pointer-into-object arithmetic can overstate the usable region. LOW/CALLER.

### 6.3 ANSI plain wrappers vs C+ checked wrappers

| Dimension | Plain (`cplus_*`, stdio.c/2/3) | Checked (`*_s`, safe_format.c) |
|---|---|---|
| Format attributes | Yes, in `cplus_libc.h:120-132` (except wide) | Yes, in `safe_format.h:35-43,60-76` |
| NULL format/dest/stream | Yes (plain stdio.c); **No** in stdio2.c/stdio3.c extras | Yes throughout |
| `%n` rejection | **No** | Yes, all `printf_s`/`fprintf_s`/`snprintf_s`/`sprintf_s` and all `scanf_s` |
| Non-literal gate | **No** | Only through unprefixed macros → `_checked` variants; direct `cplus_*_s` and all `v*` have no gate |
| `dmax` capacity check | n/a (`snprintf` size is the bound) | Yes (`CAP`) |
| UAF check on fmt/dest | **No** | Yes |
| Violation trap (`_exit(70)`) | **No** | Yes |
| Return type | `int`, C99 semantics | `int` (not `errno_t`; documented) |

---

## 7. Cross-cutting analysis (requested focus areas)

### 7.1 `fread`/`fwrite` overflow guard and errno
`cplus_fread`/`cplus_fwrite` (`stdio.c:84-96`) use
`if (size > 0 && nmemb > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }`. This is
**correct**: division avoids the multiplication overflow, the `size>0` guard avoids
divide-by-zero, and `EOVERFLOW` is the conventional choice. Verified by
`tests/adversarial/stdio_attack_NOTES.md` §1 (`SIZE_MAX,SIZE_MAX` → 0/`EOVERFLOW`).
`fread_s`/`fwrite_s` instead **clamp** (`count = bmax/size`, `safe_io.c:118,133`) with no
errno and no real-capacity validation — silent truncation is a different, weaker
contract (F5).

### 7.2 `fflush(stdin)` and update-stream sequencing
No wrapper enforces either rule. `cplus_fflush` (`stdio2.c:5`) forwards `NULL` and input
streams straight to the host; `fflush` on an input stream is undefined by ISO C (glibc
happens to discard buffered input). No wrapper inserts the mandatory
`fflush`/`fseek`/`fsetpos` between a write and a subsequent read on an update (`r+`/`w+`)
stream — the raw pattern is catalogued as `scratch/bugs56.cp` bug 265. Severity MED,
caller/runtime boundary (F6).

### 7.3 `fclose` double-close caches — bounded rings, recycled addresses
There are **two independent caches**:
1. **Plain** (`stdio.c`): `g_live_streams[256]` (linear array, silently stops tracking
   past 256) + `g_closed_streams[256]` (ring, 256-entry history). `cplus_stdio_is_closed`
   scans all 256, so no probe-chain issue, but the ring **wraps**: after 256 distinct
   closes the earliest marker is overwritten and a genuine double-close is no longer
   detected → second `fclose` on freed `FILE*`. Only `fopen/tmpfile/fdopen/freopen`
   call `cplus_stdio_register`; a stream created by any *unwrapped* host call
   (`fopen`, `popen`, `fmemopen`, `open_memstream`) that reuses a closed address is
   falsely refused (`EBADF`) → **fd leak** (confirmed, §8.1).
2. **Checked** (`safe_io.c`): two 512-slot open-addressing tables. `stream_remove()` sets
   the slot to `NULL` with **no tombstone**, so `stream_find()` stops at the first hole
   and any entry inserted past it becomes unreachable; `stream_add()` on a full table
   **overwrites `table[start]`**, evicting a marker. `cplus_fclose_s` also calls
   `fclose()` whenever `was_live` is true without checking the closed table. Combined,
   a double-close can fall through to `fclose()` on a freed pointer (confirmed, §8.2).
3. **Cross-API**: the two registries share no state. Open via `cplus_fopen`, close via
   `cplus_fclose_s` (and vice versa) leaves stale entries and defeats both guards.

### 7.4 Checked format gate — `%n`, non-literal, ANSI vs C+
`%n` rejection is solid for standard formats and is applied to both plain `_s` and
`_checked` calls. The **non-literal gate exists only in the `_checked` functions**,
which are reached exclusively through the unprefixed macros in `safe_format.h:78-104`.
Consequences:
- `cplus_printf_s(fmt, …)`, `cplus_snprintf_s(…, fmt, …)` etc. called by symbol name
  get `%n`/NULL/UAF/CAP checks but **no literal check**.
- Every `v*` variant (`vsnprintf_s`, `vsprintf_s`, `vfprintf`-style) is mapped straight to
  the non-checked symbol (`safe_format.h:100-103`) and can never be literal-gated.
- `CPLUS_SAFE_IS_LITERAL` = `__builtin_constant_p`. Confirmed on this toolchain
  (Apple Clang, `-O2` and `-O0`): literal string → `1`; `static const char f[]` → `0`;
  `const char *p = "hi"` → `0`. So a safe `const char[]` format is **falsely rejected**
  (violation → `_exit(70)` by default), while the gate promises only "literal-ness",
  not semantic safety.
- The compiler pass is the real CWE-134 control; runtime gate is defense-in-depth.
  `scratch/bugs19.cp` (455-464), `bugs20.cp` (465-474), `bugs42.cp` (685-694),
  `bugs56.cp` are ready-made regression corpora for these gates.

### 7.5 Output truncation / NUL termination
Bounded variants delegate to `vsnprintf`, so with `dmax > 0` the C99 guarantee (write at
most `dmax-1` chars + NUL) holds, and returns the would-be length. Truncation is **not
signalled** beyond `retval >= dmax`; caller must check. `dmax == 0` writes nothing and
does not NUL-terminate (standard). No checked wrapper appends its own NUL or rejects
truncation. This is documented ("bounded variants never write past dmax and always
NUL-terminate when dmax>0", `safe_format.h:12-14`) and is a caller obligation (F12).

### 7.6 Resource cleanup on failure paths
- `cplus_fopen_s` on `fopen` failure logs + stats, no leak.
- `cplus_freopen` failure follows host semantics (original stream closed by host), but
  the wrapper does not reconcile the registry for the old pointer (F9).
- `cplus_fclose_s` adds the stream to the closed table **before** calling `fclose`; if
  `fclose` fails the marker is still correct (C invalidates the stream either way).
- `fmt_violation` → `cplus_runtime_violation` calls `fflush(NULL)` then `_exit(70)`
  (`safe_runtime.c:54-60`): `atexit` handlers do not run, so `tmpfile()` temp files may
  be left behind (`U-TMP`, F10). `fflush(NULL)` also flushes unrelated streams as a
  side effect of a format violation.

---

## 8. Reproduction evidence (compiled from stdin; no repo files written)

### 8.1 Plain `cplus_fclose` false rejection + fd leak — **confirmed**
```
a = cplus_tmpfile()          -> 0x1f6cca380   close_a = 0
b = fopen("/dev/null","r")   -> 0x1f6cca380   (raw host fopen, recycled address)
cplus_fclose(b)              -> -1, errno = 9 (EBADF)   <-- valid first close refused
host fclose(b)               -> 0                       <-- proves wrapper leaked the fd
```
Root cause: `cplus_tmpfile` registered `a`; `cplus_fclose(a)` left its address in the
256-entry closed ring; raw `fopen` reused the address without calling
`cplus_stdio_register`, so the closed marker was never cleared. (Plain creators
`tmpfile/fdopen/freopen` now do register — `stdio3.c:20,27,36` — but unwrapped host
creators still do not.)

### 8.2 Checked `cplus_fclose_s` guard defeated → UAF — **confirmed**
```
colliding bucket = 127 ; A = 0x1f6cca418 ; B = 0x7cc8c063a8   (same hash)
1st close_s(A) = 0 ; 1st close_s(B) = 0
reopen at A's address (cplus_fopen_s) -> A address reused; closed-table slot 127 cleared
2nd close_s(B) = -1, errno = 9 (EBADF)   <-- expected EOF/EINVAL(22); instead host
                                            fclose() ran on already-freed B
```
Root cause: `stream_remove()` writes `NULL` (no tombstone), breaking the probe chain;
`B` also stayed stale in the live table, so `was_live=1` caused `cplus_fclose_s` to skip
the refusal branch and call `fclose(B)` again. Under ASan this is a use-after-free.

### 8.3 Toolchain literal-gate probe — **confirmed**
```
-O2:  IS_LIT("hi")=1   IS_LIT(static const char f[]) = 0   IS_LIT(const char *p="hi") = 0
-O0:  IS_LIT("hi")=1
```

---

## 9. Severity counts

| Severity | Count | Finding IDs |
|---|---|---|
| **HIGH** | **3** | F1 checked-close UAF; F2 plain-close false reject / fd leak + cross-registry bypass; F3 bounded/evicted caches |
| **MED** | **7** | F4 unvalidated stdio2/stdio3 delegates; F5 `fread_s`/`fwrite_s` bmax trust; F6 `fflush(stdin)`/update sequencing; F7 literal-gate reliability & `v*`/direct bypass; F8 `scanf_s` Annex-K divergence; F9 `freopen`/`fdopen` registry reconciliation; F10 default `_exit(70)` trap on gate false positive |
| **LOW** | **5** | F11 raw formats ungated by design; F12 truncation/NUL caller duty; F13 `sprintf_s`/`fopen_s` API-shape divergence; F14 `%n` parser non-standard lengths; F15 `fmt_check_dest_bounds` pointer-into-object |

**Total: 15 findings — 3 HIGH, 7 MED, 5 LOW.** (F10's temp-file cleanup aspect is LOW,
but the default trap's process termination raises the overall finding to MED.)

By layer: **runtime 8** (F1,F2,F3,F4,F5,F9,F10,F15), **compiler/tooling 3**
(F7,F8,F14), **caller/contract 4** (F6,F11,F12,F13).

---

## 10. Top 5 prioritized issues — concrete fixes and tests

### #1 — HIGH: `cplus_fclose_s` double-close UAF (F1)
**Fix:** in `safe_io.c`, (a) use tombstones or robin-hood/backward-shift deletion in
`stream_remove` so probe chains stay intact; (b) make `cplus_fclose_s` refuse when the
pointer is in **either** the live or closed table with an unexpected state
(`was_live && in_closed` → violation); (c) add a monotonically increasing generation ID
per `FILE*` so recycled addresses cannot alias old entries; (d) on table-full, fail
closed (record violation) instead of `table[start]=f`.
**Test:** port §8.2 into `tests/crt/unit/test_fclose_s.c`: force a bucket collision,
close A then B, reopen at A's address, double-close B, assert `EOF/EINVAL` (22) and no
second host `fclose`. Run under ASan: no UAF report. Add a 600-stream collision stress
loop modelled on the prior probe in `tests/adversarial/stdio_attack_NOTES.md` FINDING 2.

### #2 — HIGH: plain `cplus_fclose` false rejection / fd leak + cross-registry bypass (F2)
**Fix:** replace address-only liveness with generation-tagged identity shared by both
layers (move the cache into one internal module used by `stdio.c` and `safe_io.c`).
Ensure every creation path registers/unmarks, and add an explicit "unknown stream" path:
if a `FILE*` is not in the live table but also not in the closed ring, allow the first
close and record it, rather than relying on history. Document that `fclose`/`fclose_s`
must not be mixed across layers with the same handle.
**Test:** port §8.1 into `tests/libc/test_stdio_attack.c` (the existing
`attack_close_reuse()` already targets tmpfile reuse): assert `fd count` returns to
baseline after a raw-`fopen` reuse + `cplus_fclose`. Add a cross-API test that closes a
`cplus_fopen` handle with `cplus_fclose_s` and asserts a second close of any kind is
refused.

### #3 — HIGH: bounded/evicted double-close caches (F3)
**Fix:** remove fixed 256/512 hard caps as security boundaries — use a dynamically grown
table or an LRU with explicit "evicted → fail closed" semantics. When a marker must be
dropped, refuse to trust the guard for that address (or quarantine the address) instead
of silently forgetting it.
**Test:** close 257 distinct streams via `cplus_fclose` then double-close the first;
assert refusal. Fill 513 `cplus_fclose_s` closed slots then double-close an early stream;
assert `EOF/EINVAL`. Verify `/dev/fd` baseline is restored in both.

### #4 — MED: checked-format literal gate reliability (F7)
**Fix:** (a) expose and document that only the unprefixed macros provide the literal
gate; consider making `cplus_printf_s`/`cplus_snprintf_s` themselves call the `_checked`
variants with `fmt_is_literal` forced to `0` unless a macro supplies it (or rename them
to make the distinction obvious); (b) replace `__builtin_constant_p` with a compiler
pass / `__attribute__((format))` as the primary control (as `cplus_libc.h:216-220`
already states) and treat the runtime check as advisory; (c) accept `const char[]`
formats by checking `CPLUS_SAFE_IS_LITERAL` on the decayed value only when the compiler
can prove it, otherwise log rather than `_exit(70)` for the false-positive class.
**Test:** add compile probes using `scratch/bugs19.cp` (455-464), `bugs42.cp` (685-694),
`bugs56.cp`: non-literal `%n` format must be rejected, literal `%n` must be rejected,
`printf_s("%s %d",…)` accepted. Add a `const char fmt[]` case to assert the chosen
policy (accept or controlled diagnostic, not silent `_exit`). Verify `vsprintf_s` alias
path is documented as ungated.

### #5 — MED: `fread_s`/`fwrite_s` trust `bmax` and silently clamp (F5)
**Fix:** mirror the format API: add `known_bmax` (from `CPLUS_SAFE_OBJ_SIZE(buf)`),
refuse when `bmax > known capacity`, set `errno = EINVAL` on all invalid arguments, and
distinguish "clamped" from "success" (e.g., record a stat/log, or return an error when
`bmax/size < count` rather than silently reading fewer items). Treat `size==0 ||
count==0` as a valid no-op returning 0, not an "invalid args" error.
**Test:** in `tests/crt/unit/test_fwrite_s.c` add guarded canary buffers and assert:
`bmax=16, size=SIZE_MAX, count=SIZE_MAX` writes nothing and reports a defined refusal;
`bmax > __builtin_object_size(buf)` → `0`/`EINVAL`; `size=0` and `count=0` → `0` with no
"invalid args" log. Keep the existing `EOVERFLOW` test for the plain wrappers unchanged.

---

## 11. Responsibility split (runtime / compiler / caller)

### Runtime (library must own)
- NULL stream/buffer/format guards where provided (`stdio.c`, all `*_s`).
- `size*nmemb` overflow → `EOVERFLOW` (`cplus_fread`/`fwrite`) — **working**.
- `bmax`/`dmax` clamp and capacity checks — partial (missing in checked I/O).
- `%n` rejection, scanf width enforcement, UAF-on-freed-pointer checks — present in
  checked format wrappers.
- Double-close protection — **defective** (F1/F2/F3).
- Logging/stats parity on/off — verified by `tests/family_gaps/stdio_checked.c`.

### Compiler / toolchain
- `format(printf|scanf,…)` attributes catch type/count mismatches at compile time for
  all plain and checked narrow wrappers (`cplus_libc.h:120-132`, `safe_format.h:35-43`).
- **Wide formats have no attribute support on Apple Clang** (`cplus_libc.h:223-224`).
- `CPLUS_SAFE_IS_LITERAL` (`__builtin_constant_p`) is the runtime literal gate's only
  source of truth; it is conservative and toolchain-dependent (§8.3).
- Runtime variadic mismatches (`va_arg` type errors) are **not** detectable here.

### Caller
- Validity/liveness of raw `FILE*`, NUL-termination of `str`/`fmt`, and matching of
  variadic argument types/counts.
- Correct `dmax`/`bmax` values (checked I/O; format capacity is partially verified).
- Update-stream sequencing and `fflush` semantics (F6).
- Choosing the gated API (`*_s` macro) when CWE-134/CWE-120 resistance is required;
  using plain `cplus_printf`/`cplus_sprintf` forfeits those gates (F11).
- Checking return values for truncation (`retval >= dmax`) and short I/O (F12).

---

## 12. Evidence index

- `libc/src/stdio/stdio.c` — plain wrappers, registries, overflow guard.
- `libc/src/stdio/stdio2.c` — unvalidated delegates.
- `libc/src/stdio/stdio3.c` — stream extras, creator registration.
- `libc/src/stdio/stdio_internal.h` — registration contract comment.
- `stdlib/safe/src/safe_io.c` — hash/open-addressing caches, checked I/O.
- `stdlib/safe/src/safe_format.c` — `%n`/scanf parsers, capacity/UAF gates, `_checked` variants.
- `stdlib/safe/safe_format.h` — attributes, macros, literals, `OBJ_SIZE`.
- `stdlib/safe/safe_io.h` — checked I/O prototypes/macro aliases.
- `stdlib/safe/src/safe_runtime.c:49-61` — violation → `fflush(NULL)`/`_exit(70)`.
- `stdlib/safe/src/safe_alloc.c:426-443` — `ptr_was_freed` / `ptr_size`.
- `libc/install-include/cplus_libc.h:118-140,167-171,190,207-208,216-269` — declarations,
  attributes, documented caller responsibilities.
- `tests/adversarial/stdio_attack_NOTES.md` — prior FINDING 1/2 (now partly remediated in
  source; §8 of this document re-establishes current behavior).
- `tests/crt/unit/{test_fclose_s,test_fwrite_s,test_vsnprintf_s}.c` — existing unit tests.
- `tests/family_gaps/stdio_checked.c`, `tests/libc/test_format_attack.c` — contract/logging-parity
  and checked-gate coverage.
- `scratch/bugs{19,20,42,56}.cp` — ready-made format bug corpora for regression tests.

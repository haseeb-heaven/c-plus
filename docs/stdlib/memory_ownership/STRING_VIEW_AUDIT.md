# `string` / `string_view` hardening audit (Task 2, Phase 3)

Owner: phase-3 subagent. Files owned for this task:
`stdlib/string.hp` and this document only. No test, compiler, runtime, or other
header was modified; no `git add`/`commit` was run.

## 1. Scope and layout verification

The task note warned that there might be no separate `stdlib/string_view.hp`.
Verified first:

```
$ ls stdlib/
... string.hp ...            # no string_view.hp exists
$ grep -n "class string_view" stdlib/string.hp
80:class string_view {
```

`string_view` is defined in `stdlib/string.hp:80-114`; all findings and the fix
below therefore live in that one header.

Audit surface:

| Symbol | Source (pre-fix lines) |
| --- | --- |
| `stdlib_utf8_validate` | `string.hp:30-77` |
| `string_view` ctor / `size` / `data` / `equals` / `is_valid_utf8` | `string.hp:80-114` |
| `string` ctor / `size` / `capacity` / `valid` / `at` / `c_str` | `string.hp:116-160` |
| `equals` / `compare` / `find` / `reserve` / `append` | `string.hp:162-287` |
| `clear` / `copy_from` / `is_valid_utf8` | `string.hp:289-326` |
| `substr` / `count_fields` / `field` / `to_view` / `drop` | `string.hp:328-398` |

Working-tree note: before this task `stdlib/string.hp` already carried
uncommitted hardening in `compare`, `substr` and `field` (replacing raw
`other[0]` / `out[n] = '\0'` writes with `__builtin_memcpy`). Those edits were
**preserved verbatim**; they are not part of the phase-3 change.

## 2. Method and exact commands

All builds/runs were wrapped in `timeout 30` from the repository root
(`build/c+` is the checked-in compiler). Four committed regression fixtures plus
one scratch adversarial probe were exercised. Mode names map to driver flags:

* `strict-none` → `--strict --sanitize=none`
* `ubsan` → `--strict --sanitize=undefined`

Exact command template (shown with concrete paths for one case):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
SCRATCH=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/string-audit

timeout 30 build/c+ build tests/stdlib/deep_string_append.cp \
  --strict --sanitize=none -o "$SCRATCH/deep_string_append_none"
timeout 30 "$SCRATCH/deep_string_append_none"
```

The four fixture sources are:

```
tests/stdlib/deep_string_append.cp
tests/stdlib/deep_string_view.cp
tests/stdlib/deep_string_task4.cp
tests/stdlib/agent2/extra_string_vector_alias.cp
```

Scratch probe (not committed; kept under the harness-approved temp directory):
`$SCRATCH/probe_harden.cp` — 43 self-checking assertions covering all eleven
investigation categories with `#define STDLIB_ALLOC_LIMIT ((size_t)4096)` so the
overflow branches are reached with small inputs. Stale views are checked **only**
by pointer identity and `stdlib_alloc_generation` /
`stdlib_alloc_check_generation`; no stale view is ever dereferenced (no
`equals()` / `is_valid_utf8()` on a moved-from or dropped buffer).

## 3. Investigation matrix (evidence)

Legend: **OK** = behaviour confirmed by a passing assertion; **FIXED** = defect
reproduced and repaired; **RISK** = documented residual risk, no observable
misbehaviour on this backend.

| # | Category | Reproduction / observation | Result |
| --- | --- | --- | --- |
| 1 | append self-alias | `s="abcdef"; s.append(s.c_str())` → `"abcdefabcdef"`, capacity 14; 6× `g.append(g.c_str())` → 64 `'x'` bytes (`deep_string_append:117-135`); suffix `s.c_str()+4` → correct (`deep_string_append:137-160`, probe `self-full`, `self-suffix`) | OK |
| 2 | overlapping source/dest | After `s=string("abcdefgh"); p=s.c_str()+2; s.clear(); s.append(p)`, source `[2,8)` overlaps dest `[0,6)`. Pre-fix alias test only watched `[chars, chars+count]` (= `[chars,chars]` after `clear()`), so this took the `memcpy` branch — standard UB even though forward copying produced `"cdefgh"` on this backend (probe `overlap-setup`, `overlap-result`, `overlap-nul`; also prior audit §7 item 1). | **FIXED** |
| 3 | capacity calculations | `cap-initial` (3), `cap-at` (reserve(3) no-op), `cap-below` (reserve(2) no-op), exact-fill vs doubling (`deep_string_append:162-168`, `deep_string_task4:193-229`, probe `cap-*`) all match the documented doubling-clamped-to-need rule | OK |
| 4 | NUL termination | `c_str()[size()] == '\0'` after every append path, after `clear()`, after overflow recovery; `substr`/`field` always terminate (`t4_sub_*`, probe `*-nul`) | OK |
| 5 | UTF-8 boundaries | `C2 80`, `DF BF`, `E0 A0 80`, `ED 9F BF`, `EE 80 80`, `F0 90 80 80`, `F4 8F BF BF` accepted; `C0 80`, `E0 9F BF`, `F0 8F BF BF`, `ED A0 80`, `F4 90 80 80`, `F5 …`, truncated `E2 82`, stray `80`, `NULL,3` rejected; embedded NUL accepted (`deep_string_view:93-112`, probe `utf8-*`) | OK |
| 6 | slicing | `start > size` → `-1`; `start == size` → empty; `n=(size_t)-1` clamps; `out_size==0` → `-1`; `out_size==1` → empty; field with separator first/last and huge index (`t4_substr_field`, probe `slice-*`, `field-huge-index`) | OK |
| 7 | empty views | `string_view(NULL,0)`: size 0, `data()==NULL`, `equals("")==1`, UTF-8 valid; `string_view(NULL,3)`: UTF-8 invalid; `to_view()` of empty / failed-ctor string is an empty window (`deep_string_view:138-169`, `extra_string_vector_alias:148-164`, probe `view-empty`) | OK |
| 8 | stale views (metadata only) | After reallocating `reserve`, `v.data()` still equals old pointer, `v.size()` unchanged, `stdlib_alloc_check_generation(old,gen)==0`; after `drop()` the old buffer is no longer live. Never dereferenced (`deep_string_view:171-201`, `deep_string_task4:389-400`, `extra_string_vector_alias:166-215`, probe `view-*`) | OK |
| 9 | invalid indices | `at((size_t)-1)=='\0'`; `substr(start>size)==-1`; `field(index=(size_t)-1)==-1` (`deep_string_task4:322-355`, probe `at-huge-index`, `field-huge-index`) | OK |
| 10 | large length overflow | With limit 4096: `reserve(4097)==-1` with zero allocation and unchanged buffer; `append` of 2999 bytes succeeds once, second returns `-1` and leaves count 3003 and terminator intact (`deep_string_append:253-273`, `deep_string_task4:264-299`, probe `ovf-*`) | OK |
| 11 | failure atomicity (adjacent) | Injected `stdlib_alloc_fail_at(1)` leaves pointer, size, capacity and bytes byte-identical for `append`, `reserve`, `copy_from`; telemetry alloc==free (`deep_string_append:275-328`, `deep_string_task4:145-185`) | OK |

## 4. Defect reproduced and fix

**Defect (reachable standard-level memory-safety defect, no observable wrong
bytes on this backend): overlapping `memcpy` in `string::append` for an interior
source pointer past `count`.**

Before the fix, `append` decided whether to use `memmove` with:

```c
if (this->chars != NULL && (uintptr_t)text >= (uintptr_t)this->chars &&
    (uintptr_t)text <= (uintptr_t)this->chars + this->count) { ... }
```

After `clear()` the live prefix `[chars, chars+count]` collapses to the single
point `chars`, but the allocation `[chars, chars+space)` still holds the old
bytes. An interior pointer such as `p = s.c_str()+2` is inside the allocation
yet outside the watched window, so `append` took `memcpy(chars+count, text,
extra)` with overlapping ranges `[0,extra)` and `[2,2+extra)`. `memcpy` with
overlapping objects is undefined behaviour (C/C++ standard); this libc copies
forwards, so the probe observed the right result and no sanitizer here diagnoses
memcpy overlap (harness supports `none` and `undefined` only, never ASan).

**Fix (`stdlib/string.hp:260-275`):** widen the alias window from the live
prefix to the whole allocated span and keep `memmove` for that case:

```c
if (this->chars != NULL && (uintptr_t)text >= (uintptr_t)this->chars &&
    (uintptr_t)text < (uintptr_t)this->chars + this->space) {
    aliases = 1;
    offset = (size_t)((uintptr_t)text - (uintptr_t)this->chars);
}
```

Why this is ABI/API preserving and safe:

* No signature, field, return value, error note, or allocation count changes;
  only the branch choosing `memmove` over `memcpy` changes.
* For `offset <= count` behaviour is identical to before (the previous window
  is a subset of the new one). The terminator position `chars+count` yields
  `extra == 0` and returns earlier, so it is unaffected.
* For `count < offset < space`, `strlen(text)` is bounded by the NUL inside the
  buffer, so `extra <= space - offset - 1` and `need = count + extra + 1 <=
  space`. No reallocation can occur, hence the source bytes cannot be moved or
  released before the `memmove`; reading `chars[offset .. offset+extra)` and
  writing `chars[count .. count+extra)` both stay inside the allocation.
* Address comparison remains integer-based (`uintptr_t`) so unrelated pointers
  are never relationally compared (which would itself be UB).
* The one-past-end address `chars+space` is deliberately excluded (`<`).

## 5. Verification results

All commands used `timeout 30` for both build and run. Exit codes are the raw
`timeout` results. `deep_string_append` / `deep_string_view` deliberately print
expected `C+ runtime error:` lines from forked fatal-child probes to stderr;
those children are part of the test and do not affect the parent's exit status.

### 5.1 Baseline (before the fix)

| Fixture | mode | build rc | run rc | reported result |
| --- | --- | --- | --- | --- |
| `deep_string_append` | strict-none | 0 | 0 | `append-checks 81`, `append-failures 0` |
| `deep_string_append` | ubsan | 0 | 0 | `append-checks 81`, `append-failures 0` |
| `deep_string_view` | strict-none | 0 | 0 | `view-checks 57`, `view-failures 0` |
| `deep_string_view` | ubsan | 0 | 0 | `view-checks 57`, `view-failures 0` |
| `deep_string_task4` | strict-none | 0 | 0 | `STRING_TASK4 86 0` |
| `deep_string_task4` | ubsan | 0 | 0 | `STRING_TASK4 86 0` |
| `extra_string_vector_alias` | strict-none | 0 | 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` |
| `extra_string_vector_alias` | ubsan | 0 | 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` |
| scratch `probe_harden` (first draft) | both | 0 | 1 | 2 *test-side* expectation mistakes (`cap-fill-nul` assumed no doubling; `ovf-append-overflow` miscounted 3004 vs 3003). Corrected in the scratch probe only; no library change. |
| scratch `probe_harden` (corrected) | both | 0 | 0 | `PHASE3_PROBE 43 0` |

### 5.2 After the fix (final verification)

| Fixture | mode | build cmd / rc | run cmd / rc | reported result |
| --- | --- | --- | --- | --- |
| `deep_string_append` | strict-none | `timeout 30 build/c+ build tests/stdlib/deep_string_append.cp --strict --sanitize=none -o $SCRATCH/deep_string_append_none` / 0 | `timeout 30 $SCRATCH/deep_string_append_none` / 0 | `append-success 1`, `append-recoverable 1`, `append-fatal 1`, `append-checks 81`, `append-failures 0` |
| `deep_string_append` | ubsan | same with `--sanitize=undefined` / 0 | / 0 | identical `81 / 0` |
| `deep_string_view` | strict-none | `… deep_string_view.cp --strict --sanitize=none …` / 0 | / 0 | `view-success 1`, `view-recoverable 1`, `view-fatal 1`, `view-checks 57`, `view-failures 0` |
| `deep_string_view` | ubsan | same with `--sanitize=undefined` / 0 | / 0 | identical `57 / 0` |
| `deep_string_task4` | strict-none | `… deep_string_task4.cp --strict --sanitize=none …` / 0 | / 0 | `STRING_TASK4 86 0` |
| `deep_string_task4` | ubsan | same with `--sanitize=undefined` / 0 | / 0 | `STRING_TASK4 86 0` |
| `extra_string_vector_alias` | strict-none | `… tests/stdlib/agent2/extra_string_vector_alias.cp --strict --sanitize=none …` / 0 | / 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` |
| `extra_string_vector_alias` | ubsan | same with `--sanitize=undefined` / 0 | / 0 | `EXTRA_STRING_VECTOR_ALIAS 60 0` |
| scratch `probe_harden` | strict-none | `timeout 30 build/c+ build $SCRATCH/probe_harden.cp --strict --sanitize=none -o $SCRATCH/probe_harden_none` / 0 | / 0 | `PHASE3_PROBE 43 0` |
| scratch `probe_harden` | ubsan | same with `--sanitize=undefined` / 0 | / 0 | `PHASE3_PROBE 43 0` |

Total: 10/10 build+run invocations passed after the fix (81+57+86+60+43 = 327
assertions, zero failures in each mode). No invocation timed out; no sanitizer
diagnostic appeared on stdout/stderr beyond the fatal-fork messages.

## 6. Residual risks / limitations

1. **ASan unavailable.** The harness offers only `--sanitize=none` and
   `--sanitize=undefined`; the repaired `memcpy` overlap was a standard-level
   UB finding, not an ASan report. UBSan passing does not prove absence of all
   overlap/UAF classes.
2. **`string_view` has no generation token.** Unlike `vector::slice`, it cannot
   self-report staleness; callers must use allocator metadata
   (`stdlib_alloc_check_generation`) and must not dereference a view after a
   reallocating `append`/`reserve` or `drop()`. All phase-3 tests obey this.
3. **Length-aware storage limitation.** `string` storage is `strlen`-based, so
   embedded NUL bytes cannot be stored; `string_view(ptr,len)` and
   `stdlib_utf8_validate(ptr,len)` are length-aware and were verified to accept
   embedded NULs (`sva_skip_str_nul_storage`, `utf8-embedded-nul`). Documented,
   not a defect.
4. **No thread safety.** `string` / `string_view` have no locking; concurrent
   mutation is unsupported and untested.
5. **Failure-injection/limit probes reduce `STDLIB_ALLOC_LIMIT` to 4096** in the
   fixtures and the scratch probe; production uses the `1<<40` default. This is
   the documented build-time override, not a header change.

## 7. Change summary

* `stdlib/string.hp`: one functional edit — widen the `append` alias window
  from `[chars, chars+count]` to `[chars, chars+space)` so interior sources
  past `count` use `memmove` instead of overlapping `memcpy`, plus an expanded
  explanatory comment. Pre-existing `compare`/`substr`/`field` hardening left
  untouched.
* `docs/stdlib/memory_ownership/STRING_VIEW_AUDIT.md`: this document (new).
* No other files created or modified in the repository; no git staging or
  commits; no compiler/runtime/test edits.

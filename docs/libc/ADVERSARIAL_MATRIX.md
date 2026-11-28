# Adversarial bug catalogue -> mitigation matrix

Maps all 204 bug patterns (`scratch/bugs1.cp`, `scratch/bugs2.cp`, `tests/crt/adversarial/bug_catalogue.c`, and `tests/crt/unit/test_bugs_catalogue_204.c`) to how the C+ hardened layer handles them (using standard-C SafeCRT signatures for `malloc_s`/`calloc_s`/`realloc_s`/`free_s`/`cplus_fopen_s`/`cplus_fclose_s`, `cplus_safe_len`, `cplus_safe_transfer_ownership`, and `cplus_runtime_push_cleanup`). See also [`tests/safety/SCENARIO_MATRIX.md`](../../tests/safety/SCENARIO_MATRIX.md).

Legend:
- **SAFE-CATCH** — a checked C+ API returns a defined error / avoids the UB when the caller uses it (proved by `tests/crt/adversarial/test_safe_mitigations.c`).
- **COMPILER** — requires static analysis / compile-time enforcement (ownership, lifetime, provenance, bounds on raw arrays). Agent 1 territory; not a libc guarantee.
- **RUNTIME (opt)** — caught only with runtime instrumentation (sanitizer) or checked API; raw form is UB.
- **BOUNDARY** — explicit unsafe boundary: cannot be detected by any library without allocator/compiler metadata; must be documented and avoided.

| # | Bug pattern | Class | Mitigation / evidence |
|---|---|---|---|
| 1 | basic leak (missing free) | COMPILER | RAII/ownership (Agent 1); `free_s(&p)` nulls handle |
| 2 | null deref unchecked malloc | SAFE-CATCH | `malloc_s` returns status; caller must check |
| 3 | null struct member | COMPILER | nullability analysis (Agent 1) |
| 4 | null to memcpy | SAFE-CATCH | `cplus_memcpy_s` -> ESNULLP |
| 5 | UAF read | BOUNDARY | needs allocation/compiler metadata |
| 6 | UAF write | BOUNDARY | needs allocation/compiler metadata |
| 7 | return local addr | COMPILER | lifetime analysis (Agent 1) |
| 8 | block-scope dangling | COMPILER | lifetime analysis (Agent 1) |
| 9 | compound-literal dangling | COMPILER | lifetime analysis (Agent 1) |
| 10 | realloc moved old ptr | COMPILER | `realloc_s` preserves on failure; alias invalidation is compiler-side |
| 11 | aliased dangling | BOUNDARY | alias/provenance tracking |
| 12 | double free | BOUNDARY | allocator metadata; `free_s` nulls single handle only |
| 13 | early-return leak | COMPILER | RAII (Agent 1) |
| 14 | partial-init leak | COMPILER | RAII (Agent 1) |
| 15 | file leak on early return | COMPILER | RAII; `cplus_fclose_s` used with cleanup discipline |
| 16 | mutex deadlock | COMPILER | RAII locks (Agent 1) |
| 17 | longjmp skips cleanup | COMPILER | known baseline `classic_bug34_longjmp` |
| 18 | nested struct leak | COMPILER | ownership (Agent 1) |
| 19 | shallow copy double free | BOUNDARY | `free_s` nulls handle to reduce risk |
| 20 | wild pointer | COMPILER | init analysis (Agent 1) |
| 21 | uninit struct ptr | COMPILER | init analysis (Agent 1) |
| 22 | stack overflow | COMPILER/RUNTIME | static bounds; `cplus_memcpy_s` bounded |
| 23 | heap overflow | SAFE-CATCH | bounded `memcpy_s`/`memset_s` reject |
| 24 | stack underflow | COMPILER | static bounds (Agent 1) |
| 25 | heap underflow | BOUNDARY | allocator metadata |
| 26 | off-by-one | COMPILER | static bounds; bounded API rejects |
| 27 | free stack mem | BOUNDARY | allocator/compiler metadata |
| 28 | free literal | BOUNDARY | allocator metadata |
| 29 | modify literal | COMPILER | const/rodata (Agent 1) |
| 30 | free offset ptr | BOUNDARY | allocator metadata |
| 31 | pointer overwrite leak | COMPILER | ownership (Agent 1) |
| 32 | realloc null leak | SAFE-CATCH | `realloc_s` preserves old on failure |
| 33 | uninit heap read | SAFE-CATCH (opt) | `calloc_s`/`memzero_s` deterministic |
| 34 | uninit stack read | COMPILER | init analysis (Agent 1) |
| 35 | sizeof(pointer) | COMPILER | static analysis (Agent 1) |
| 36 | missing sizeof | SAFE-CATCH | bounded `memcpy_s`/`memset_s` reject on overflow |
| 37 | missing NUL terminator | SAFE-CATCH | `strcpy_s`/`strncpy_s` reject overflow |
| 38 | non-terminated strlen | SAFE-CATCH | `strnlen_s`/`memcmp_s` bounded |
| 39 | unbounded strcpy | SAFE-CATCH | `strcpy_s` -> ESNOSPC |
| 40 | overlap memcpy | SAFE-CATCH | `memcpy_s` -> ESOVRLP; use `memmove_s` |
| 41 | int overflow malloc | SAFE-CATCH | `calloc_s` -> EOVERFLOW |
| 42 | signed->unsigned bypass | SAFE-CATCH | checked sizes reject huge `rsize_t` |
| 43 | ptr arith past end | COMPILER | provenance (Agent 1) |
| 44 | deref end ptr | COMPILER | bounds (Agent 1) |
| 45 | strict aliasing | COMPILER | UB model (Agent 1) |
| 46 | unaligned access | COMPILER | alignment (Agent 1) |
| 47 | fclose(NULL) | SAFE-CATCH | `cplus_fclose_s(NULL)` -> EINVAL |
| 48 | double fclose | SAFE-CATCH | `cplus_fclose_s` 2nd -> EINVAL |
| 49 | 2D free order | BOUNDARY | ownership (Agent 1) |
| 50 | FAM missing payload | SAFE-CATCH (opt) | bounded APIs reject; alloc size is caller's |
| 51 | malloc(0) deref | SAFE-CATCH | `malloc_s(0)` returns defined 1-byte tracked block |
| 52 | setbuf dangling | BOUNDARY | stream buffer lifetime (Agent 1) |
| 53 | list traversal UAF | COMPILER | ownership (Agent 1) |
| 54 | dlist dangling | COMPILER | ownership (Agent 1) |
| 55 | realloc interior ptr | BOUNDARY | provenance (Agent 1) |
| 56 | thread detach stack ptr | COMPILER | lifetime/threads (Agent 1) |
| 57 | pthread_cancel leak | COMPILER | cleanup handlers (Agent 1) |
| 58 | double-checked lock | COMPILER | atomics (Agent 1) |
| 59 | TOCTOU race | COMPILER | concurrency (Agent 1) |
| 60 | signal malloc/free | BOUNDARY | async-signal-safety |
| 61 | static buffer alias | BOUNDARY | lifetime (Agent 1) |
| 62 | ptr sub distinct | COMPILER | provenance (Agent 1) |
| 63 | ptr cmp distinct | COMPILER | provenance (Agent 1) |
| 64 | provenance violation | BOUNDARY | CHERI/provenance model |
| 65 | struct padding leak | COMPILER | init/`memzero_s` for scrubbing |
| 66 | memcmp padding | SAFE-CATCH (opt) | compare fields, not padding |
| 67 | switch-scope dangling | COMPILER | lifetime (Agent 1) |
| 68 | goto skips init | COMPILER | init analysis (Agent 1) |
| 69 | alloca in loop | COMPILER | stack analysis (Agent 1) |
| 70 | infinite recursion | COMPILER/RUNTIME | stack limits |
| 71 | va_arg mismatch | COMPILER | type checking (Agent 1) |
| 72 | missing sentinel | COMPILER | API contract (Agent 1) |
| 73 | arena alignment | COMPILER | alignment (Agent 1) |
| 74 | refcount underflow | COMPILER | ownership (Agent 1) |
| 75 | cyclic ref leak | COMPILER | ownership (Agent 1) |
| 76 | mmap NULL check | COMPILER | API-usage analysis (Agent 1) |
| 77 | munmap partial | BOUNDARY | OS API contract |
| 78 | mmap SIGBUS | BOUNDARY | OS API contract |
| 79 | dlclose dangling fn | BOUNDARY | dynamic linker contract |
| 80 | strtok literal | SAFE-CATCH | `cplus_strtok_s` on writable buffer; literal is const |
| 81 | snprintf truncation OOB | SAFE-CATCH | bounded format; returns needed length |
| 82 | fread missing NUL | SAFE-CATCH | check `fread_s` count, NUL-terminate by hand |
| 83 | qsort wrong indirection | COMPILER | type checking (Agent 1) |
| 84 | out-param overflow | COMPILER | type/ABI (Agent 1) |
| 85 | restrict violation | COMPILER | aliasing (Agent 1) |
| 86 | FAM by value | COMPILER | language semantics (Agent 1) |
| 87 | container_of NULL | COMPILER | pointer math (Agent 1) |
| 88 | double-ptr ownership | COMPILER | ownership (Agent 1) |
| 89 | out-param leak | COMPILER | ownership (Agent 1) |
| 90 | dangling cache | COMPILER | lifetime (Agent 1) |
| 91 | dangling callback | COMPILER | lifetime (Agent 1) |
| 92 | ring buffer off-by-one | COMPILER/RUNTIME | static bounds |
| 93 | tagged ptr deref | BOUNDARY | tagged-pointer model |
| 94 | free tagged ptr | BOUNDARY | allocator metadata |
| 95 | tree recursion overflow | COMPILER/RUNTIME | stack analysis |
| 96 | partial array cleanup | COMPILER | ownership (Agent 1) |
| 97 | pool double return | BOUNDARY | allocator metadata |
| 98 | memmove alias double free | BOUNDARY | ownership (Agent 1) |
| 99 | async IO stack dangling | COMPILER | lifetime (Agent 1) |
| 100 | format string %s mismatch | COMPILER/RUNTIME | typed format (Agent 1); use `snprintf_s` |
| 101 | getenv dangling | BOUNDARY | environment storage contract |
| 102 | putenv stack dangling | BOUNDARY | POSIX contract |
| 103 | cleanup uninit ptr | COMPILER | init analysis (Agent 1) |
| 104 | cleanup transfer double free | COMPILER | ownership (Agent 1) |

## Executed runtime proof

`tests/crt/adversarial/test_safe_mitigations.c` (built against `libcplus_safecrt.a`)
asserts, and passes, the **SAFE-CATCH** rows that are runtime-detectable:

```
ok catch_null_memcpy            (bug02, bug04)
ok catch_dest_overflow          (bug23, bug26, bug36, bug37, bug39)
ok catch_unterminated_read      (bug38)
ok catch_zero_alloc             (bug51)
ok catch_realloc_failure_preserves (bug32)
ok catch_double_free_handle     (bug19/bug98 handle-nulling)
ok catch_stdio_lifecycle        (bug47, bug48)
ok catch_format_truncation      (bug81)
ok catch_overlap                (bug40)
ok catch_size_overflow          (bug41, bug42)
ok catch_no_write_past_bound    (bug23/26 write-bound)
ok catch_deterministic_zero     (bug33)
safe_mitigations: ok
```

## Catalogue 2 (bugs 105-204)

The second 100-pattern catalogue extends the same taxonomy (size arithmetic,
format/string/buffer variants, memmove/loop/off-by-one edges, defined-behavior
edges, resource/lifetime, OS/API boundaries). Classification uses the same
three classes (SAFE-CATCH / COMPILER / BOUNDARY) as catalogue 1.

- **SAFE-CATCH** — a checked C+ API returns a defined error / stays defined when
  the caller uses it.
- **COMPILER** — requires compile-time/static enforcement (ownership, lifetime,
  provenance, bounds on raw arrays, integer/UB rules). Agent 1 territory.
- **BOUNDARY** — explicit unsafe boundary: no library can catch it without
  allocator/compiler/OS metadata; must be documented and avoided.

| # | Bug pattern | Class | Mitigation / evidence |
|---|---|---|---|
| 105 | size multiplication overflow (`dim*elem`) before alloc | SAFE-CATCH | `calloc_s` -> EOVERFLOW; `catch_calloc_overflow` |
| 106 | strtol result used unchecked as an index | COMPILER | range analysis (Agent 1) |
| 107 | uninitialised out-param read after failed alloc | SAFE-CATCH | `malloc_s` sets `*out=NULL`; `free_s(NULL)` no-op; `catch_uninit_out_param` |
| 108 | realloc result stored into source pointer (leak on fail) | SAFE-CATCH | `realloc_s` preserves owner on failure; `catch_realloc_failure_preserves` (cat 1) |
| 109 | `a*b`/`a+b` size overflow then alloc | SAFE-CATCH | checked sizes -> EOVERFLOW (bug105 family) |
| 110 | int size compared with size_t (sign flip) | COMPILER | conversion/sign analysis (Agent 1) |
| 111 | alloca sized by attacker input | COMPILER | stack analysis (Agent 1) |
| 112 | VLA with attacker-controlled length | COMPILER | stack analysis (Agent 1) |
| 113 | recursion depth driven by input | COMPILER/RUNTIME | static depth bounds; stack limit |
| 114 | oversized stack frame | COMPILER | frame-size analysis (Agent 1) |
| 115 | memcpy length taken from untrusted field | SAFE-CATCH | bounded `memcpy_s` rejects over-size |
| 116 | fread into fixed buffer without clamp | SAFE-CATCH | `fread_s` clamps count to `bmax/size` |
| 117 | fwrite of computed length past buffer | SAFE-CATCH | `fwrite_s` clamps count to `bmax/size` |
| 118 | send/recv length vs buffer mismatch | BOUNDARY | socket API contract |
| 119 | strtol without errno/endptr check | COMPILER | parse/range analysis (Agent 1) |
| 120 | atoi/atol overflow silently truncated | BOUNDARY | libc API contract; use `strtol` + errno |
| 121 | sscanf `%s` unbounded into array | COMPILER | typed/bounded format (Agent 1); use width `%<n>s` |
| 122 | `%n` format writes through pointer | COMPILER | `%n` audit (Agent 1) |
| 123 | format string taken from user input | COMPILER | format-string analysis (Agent 1) |
| 124 | printf missing a conversion argument | COMPILER | format/arg checking (Agent 1) |
| 125 | printf extra argument (type confusion) | COMPILER | format/arg checking (Agent 1) |
| 126 | wrong length modifier (`%ld` with int) | COMPILER | format checking (Agent 1) |
| 127 | snprintf return ignored, assumed success | SAFE-CATCH (opt) | bounded; returns would-be length; caller must test |
| 128 | snprintf return used as bytes written | SAFE-CATCH | truncation contract: result may exceed buffer |
| 129 | sprintf where no room for NUL | SAFE-CATCH | bounded variant NUL-terminates; `catch_sprintf_off_by_one` |
| 130 | va_list reused after consumption | COMPILER | variadic discipline (Agent 1) |
| 131 | vprintf passed the wrong va_list | COMPILER | variadic discipline (Agent 1) |
| 132 | va_arg type mismatch | COMPILER | variadic type checking (Agent 1) |
| 133 | missing varargs sentinel (exec*, X) | COMPILER | API contract (Agent 1) |
| 134 | argv not NUL-terminated | COMPILER | API contract (Agent 1) |
| 135 | system() built from attacker input | BOUNDARY | OS/API contract; prefer exec* |
| 136 | popen mode-string injection | BOUNDARY | OS/API contract |
| 137 | strtok on a const string literal | SAFE-CATCH | `strtok_s` needs a writable buffer; literal is const |
| 138 | strtok_s `dmax` left stale across calls | SAFE-CATCH | bounded `strtok_s` validates `*dmax` |
| 139 | strsep/strtok modifying a literal | COMPILER | const/rodata (Agent 1) |
| 140 | strtok not reentrant across threads | BOUNDARY | API threading contract; use `strtok_s` |
| 141 | strcat without destination bound | SAFE-CATCH | `strcat_s` -> ESNOSPC |
| 142 | strncat called with the wrong length | SAFE-CATCH | `strncat_s` validates; `catch_strncat_loop_overflow` |
| 143 | sprintf into a too-small buffer | SAFE-CATCH | bounded `sprintf_s` reports needed length; `catch_sprintf_off_by_one` |
| 144 | strcpy of exactly `dmax` bytes (no NUL room) | SAFE-CATCH | `strcpy_s` needs room for NUL -> ESNOSPC |
| 145 | strncpy without NUL then strlen | SAFE-CATCH | `strncpy_s` always NUL-terminates |
| 146 | strlcpy return used as capacity | BOUNDARY | API return semantics |
| 147 | strcpy onto an overlapping region | SAFE-CATCH (opt) | checked copy rejects bad ranges; true aliasing is caller contract |
| 148 | substring copy with the wrong length | SAFE-CATCH | bounded `strncpy_s` rejects overflow |
| 149 | manual NUL placed past end of buffer | COMPILER | bounds (Agent 1) |
| 150 | off-by-one in in-place string reverse | COMPILER | static bounds (Agent 1) |
| 151 | itoa/dtoa into a fixed buffer | SAFE-CATCH | bounded format; returns needed length |
| 152 | manual hex/base64 encode overflow | SAFE-CATCH | bounded `memcpy_s` / index checks reject |
| 153 | write past a sub-object member in a struct | SAFE-CATCH | bounded copy rejects; `catch_subobject_overflow` |
| 154 | pointer to a struct member escaped | COMPILER | ownership (Agent 1) |
| 155 | union type punning UB | COMPILER | type model (Agent 1) |
| 156 | bitfield overflow on assignment | COMPILER | type model (Agent 1) |
| 157 | shift by >= type width | COMPILER | UB analysis (Agent 1) |
| 158 | signed integer overflow in a counter | COMPILER | signed-overflow UB (Agent 1) |
| 159 | left shift of a negative value | COMPILER | UB analysis (Agent 1) |
| 160 | division by zero | COMPILER | UB analysis (Agent 1) |
| 161 | modulo by zero | COMPILER | UB analysis (Agent 1) |
| 162 | float-to-int out of range | COMPILER | conversion UB (Agent 1) |
| 163 | NaN / signaling float compare | COMPILER | FP model (Agent 1) |
| 164 | pointer arithmetic overflowing the object | COMPILER | provenance (Agent 1) |
| 165 | comparing pointers to distinct objects | COMPILER | provenance (Agent 1) |
| 166 | negative array index | COMPILER | bounds (Agent 1) |
| 167 | loop bound off-by-one | COMPILER | bounds (Agent 1) |
| 168 | overlapping region to memcpy (should be memmove) | SAFE-CATCH | `memcpy_s` -> ESOVRLP; use `memmove_s`; `catch_overlap` (cat 1) |
| 169 | memcpy source/dest swapped | SAFE-CATCH | bounded sizes reject the bad direction |
| 170 | memcmp across struct padding | SAFE-CATCH (opt) | compare fields; `memzero_s` scrubs |
| 171 | memset with size larger than object | SAFE-CATCH | `memset_s` bounded rejects |
| 172 | memset value wider than char semantics | BOUNDARY | API value semantics |
| 173 | off-by-one memmove shift | SAFE-CATCH | `memmove_s` rejects overflow; `catch_memmove_shift_overflow` |
| 174 | variadic sentinel/type misuse | BOUNDARY | caller variadic contract; `catch_variadic_bounds` |
| 175 | blit with the wrong element size | SAFE-CATCH | bounded `memcpy_s` rejects over-size |
| 176 | copy of a pointer array with wrong count | SAFE-CATCH | bounded `memcpy_s` rejects over-size |
| 177 | 2D array flattened with the wrong stride | SAFE-CATCH (opt) | bounded copy; stride math is caller's |
| 178 | FAM payload copy with the wrong size | SAFE-CATCH (opt) | bounded copy; alloc size is caller's |
| 179 | memset of zero length with NULL | SAFE-CATCH | zero-length defined, no deref; `catch_zero_len_null` |
| 180 | memcpy of zero length with NULL | SAFE-CATCH | zero-length defined, no deref; `catch_zero_len_null` |
| 181 | closure/pointer escaping a stack frame | COMPILER | lifetime (Agent 1) |
| 182 | callback invoked after free | COMPILER | lifetime (Agent 1) |
| 183 | global-state reentrancy / data race | COMPILER | concurrency (Agent 1) |
| 184 | strncat loop passing total size each time | SAFE-CATCH | `strncat_s` rejects instead of smashing; `catch_strncat_loop_overflow` |
| 185 | path built with snprintf ignoring truncation | SAFE-CATCH | bounded; returns needed length to retry |
| 186 | TOCTOU between access() and open() | BOUNDARY | OS race; use O_EXCL/openat (Agent 1) |
| 187 | mkstemp then reopen by name | BOUNDARY | OS race |
| 188 | symlink swap between check and open | BOUNDARY | OS/API contract |
| 189 | predictable temp filename | BOUNDARY | OS/API contract |
| 190 | umask/permission race | BOUNDARY | OS contract |
| 191 | zero-length copy with NULL pointers | SAFE-CATCH | defined no-deref contract; `catch_zero_len_null` |
| 192 | repeated free guarded only by a flag | BOUNDARY | allocator metadata; `free_s` idempotent on a NULL handle |
| 193 | errno read after an unrelated call | COMPILER | API-discipline analysis (Agent 1) |
| 194 | double close of a file descriptor | BOUNDARY | OS/API contract |
| 195 | close(-1) | BOUNDARY | OS/API contract |
| 196 | file-descriptor leak in a loop | COMPILER | RAII (Agent 1) |
| 197 | FILE* used after fclose | COMPILER | lifetime (Agent 1) |
| 198 | fflush after fclose | COMPILER | lifetime (Agent 1) |
| 199 | realloc(ptr, 0) undefined outcome | SAFE-CATCH | `realloc_s` defined non-NULL; `catch_realloc_zero_defined` |
| 200 | calloc(0, N) / malloc(0) semantics | SAFE-CATCH | `calloc_s`/`malloc_s` defined; `catch_zero_alloc` (cat 1) |
| 201 | huge malloc then touch (overcommit) | BOUNDARY | OS overcommit / allocator policy |
| 202 | alloca pointer escaping its scope | COMPILER | lifetime (Agent 1) |
| 203 | signal handler calling malloc/free | BOUNDARY | async-signal-safety |
| 204 | thread-local teardown ordering | COMPILER | lifetime/threads (Agent 1) |

### Executed runtime proof (Catalogue 2)

`tests/crt/adversarial/test_safe_mitigations2.c` (built against
`build/safe/libcplus_safecrt.a`, run by `sh tests/crt/run_all_crt.sh`) asserts and
passes the runtime-detectable Catalogue 2 rows:

```
ok catch_calloc_overflow            (bug105, bug109)
ok catch_uninit_out_param           (bug107)
ok catch_sprintf_off_by_one         (bug129, bug143)
ok catch_subobject_overflow         (bug153)
ok catch_memmove_shift_overflow     (bug173)
ok catch_strncat_loop_overflow      (bug142, bug184)
ok catch_zero_len_null              (bug179, bug180, bug191)
ok catch_realloc_zero_defined       (bug199)
ok catch_variadic_bounds            (bug174, bug185)
safe_mitigations2: ok
```

The remaining Catalogue 2 rows are COMPILER (static/ownership/lifetime/integer-UB
reasoning; Agent 1) or BOUNDARY (POSIX/OS, allocator, threading and
signal-safety contracts) and are **not** runtime-catchable by a C library alone.

## Honest conclusion

- The checked API layer catches the **runtime-detectable** subset across both
  catalogues (null, bounds, overlap, truncation, zero-alloc, realloc-failure,
  double-close, size/arithmetic overflow, zero-length-with-NULL).
- Most of the 104 patterns are **compile-time/ownership/lifetime/provenance or
  explicit unsafe boundaries** and are **not** caught by a C library alone. They
  require Agent 1 compiler enforcement and/or sanitizers.
- No Rust-grade memory-safety claim is made for raw C.

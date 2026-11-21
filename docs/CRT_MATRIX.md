# CRT contract matrix (#22)

> Evidence-based. "Safe" is never claimed for raw C; each row states the
> protection class, boundary, and how it was verified.

## Legend

- **SAFE+ACCEPTED**: valid case accepted by native libc and C+ build; behavior matches contract.
- **UNSAFE+REJECTED**: invalid case rejected by checked API / runtime constraint / platform mechanism with documented error.
- **EXPLICIT UNSAFE BOUNDARY**: cannot be made safe without compiler proof or is undefined in C; not executed as UB.
- **KNOWN BUG/NOT YET SUPPORTED**: gap recorded honestly.

## Native capability probe (this host)

Generated via `tests/crt/native_safety_probe.c` and `tests/crt/probe_native.c`:

| capability | observed (macOS arm64, Apple clang 16) |
|---|---|
| _FORTIFY_SOURCE | 2 |
| __builtin_object_size | yes |
| __builtin_dynamic_object_size | yes |
| Annex K __STDC_LIB_EXT1__ | none |
| ASan compile feature | no (feature macro in normal build) |
| UBSan compile feature | no |
| TSan compile feature | no |
| bounds_safety | no |
| Apple libc | yes |
| UCRT | no |
| glibc/musl | no/no |

ASan runtime on this host is unusable for the preloaded C+ runtime environment (`CHECK failed: sanitizer_malloc_mac.inc` / preloading issue recorded in prior runs); treat ASan as **KNOWN BUG/NOT YET SUPPORTED on this host** and require Linux CI.

## API matrix

| API | Contract (owner/extent/lifetime/error) | Safe+Accepted evidence | Unsafe+Rejected / boundary | Class | Notes |
|---|---|---|---|---|---|
| `malloc` | returns owned object or NULL; owner must be freed once | `alloc.c`, `adversarial_defined.c` | overflow/oom -> NULL; double-free/UAF/alias invalidation are UB | ACCEPTED + EXPLICIT UNSAFE BOUNDARY | no alias provenance proof |
| `calloc` | same as malloc; zeroed; count*size overflow must not allocate wrong size | `alloc.c`, `test_safe_alloc.c` | overflow returns NULL/ENOMEM (`calloc_s`) | ACCEPTED/REJECTED | some impls may not detect all overflows; `calloc_s` checks |
| `realloc` | on success new owner, old invalid; on failure old owner preserved | `alloc.c`, `contract_violations.c`, `test_safe_alloc.c` | failure preserves owner; zero-size semantics platform-defined | ACCEPTED + boundary | `realloc_s` preserves owner |
| `free` | frees valid owned heap pointer or NULL | `alloc.c` | double free/UAF/invalid pointer UB | EXPLICIT UNSAFE BOUNDARY | `free_s` nulls owner location only |
| `memcpy` | src/dst valid, non-overlap, extents valid | `memory_ops.c` | overlap UB; size>dmax rejected by `cplus_memcpy_s` | ACCEPTED + REJECTED/BOUNDARY | checked variant uses Intel safestringlib |
| `memmove` | allows overlap, valid extents | `memory_ops.c`, `adversarial_defined.c` | invalid extents rejected by `memmove_s` | ACCEPTED/REJECTED | |
| `memset` | valid dst extent | `memset_ops.c`, `test_safe_memory.c` | n>dmax rejected by `memset_s` | ACCEPTED/REJECTED | |
| `memcmp_s` | valid extents, diff out | `test_safe_memory.c` | invalid args error | ACCEPTED/REJECTED | |
| `memzero_s` | valid dst extent | `test_safe_memory.c` | invalid args error | ACCEPTED/REJECTED | |
| `strcpy` | dst has enough capacity incl NUL; src NUL-terminated | `string_ops.c` | insufficient capacity is UB for raw strcpy | EXPLICIT UNSAFE BOUNDARY | use `strcpy_s` or proven bounds |
| `strncpy` | bounded copy; may not NUL-terminate | `strncpy_ops.c` | truncation/no-NUL pitfall | ACCEPTED + boundary | |
| `strcat` | dst has capacity for existing+src+NUL | `string_ops.c` | overflow UB raw | EXPLICIT UNSAFE BOUNDARY | checked `strcat_s` |
| `strlen`/`strnlen_s` | str readable until NUL/smax | `string_ops.c`, `test_safe_string.c` | NULL -> 0 for `strnlen_s` | ACCEPTED/REJECTED | |
| `printf/snprintf/fprintf` | format valid; output buffer extent valid | `printf_ops.c`, `test_safe_format.c` | truncation reported by snprintf; format mismatch UB in raw varargs misuse | ACCEPTED + boundary | wrappers do not parse format |
| `scanf/fscanf` | pointers valid for conversions; bounded `%Ns` | `format_ops.c`, `test_safe_format.c` | invalid conversion returns 0/EOF; unbounded `%s` unsafe | ACCEPTED + EXPLICIT UNSAFE BOUNDARY | |
| `fopen/fclose/fread/fwrite` | valid path/mode; FILE* lifetime unique/valid | `stdio_ops.c`, `test_safe_io.c`, `contract_violations.c` | missing file NULL+errno; invalid handle UB | ACCEPTED + boundary | no double-close proof in raw C |

## Checked extension APIs (C+ optional, not standard CRT)

| API | Contract | Evidence | Error class |
|---|---|---|---|
| `malloc_s` | out!=NULL, bytes>0 normalized, returns CPLUS_SAFE_* | `test_safe_alloc.c`, examples | EINVAL/ENOMEM |
| `calloc_s` | overflow checked, zeroed | `test_safe_alloc.c` | EOVERFLOW/ENOMEM |
| `realloc_s` | owner preserved on failure | `test_safe_alloc.c` | ENOMEM/EINVAL |
| `free_s` | nulls owner location, no-op on NULL | `test_safe_alloc.c` | boundary: aliases remain invalid |

## Adversarial/UB boundaries (NOT executed)

These are **EXPLICIT UNSAFE BOUNDARY** unless Agent 1 proves them in safe mode:

- Double free / UAF / alias-after-free
- Out-of-bounds write/read through raw pointer
- Overlapping memcpy
- `fclose` twice / use after close
- Unbounded `%s` in scanf into fixed buffer
- Writing through invalidated realloc old pointer
- Data races on FILE*/objects without synchronization

## Test commands

```sh
sh tests/crt/run_crt_tests.sh          # plain C CRT conformance corpus
sh tests/crt/run_safecrt_tests.sh      # safe CRT wrapper unit tests
sh build/safe/build_safecrt.sh         # build libcplus_safecrt.a
# examples
for f in examples/crt/*.c; do clang -Istdlib/safe -Ithird_party/intel-safestringlib/include "$f" build/safe/libcplus_safecrt.a -o /tmp/ex; /tmp/ex; done
```

## Known gaps

- ASan/libFuzzer unavailable/broken on this macOS host for this runtime layout; Linux CI required for #23.
- Intel safestringlib subset is vendored but not all Annex K APIs exposed.
- `scanf_s`/format APIs are wrappers; they do not provide full Annex K constraint semantics on all platforms.
- No compiler proof for alias/UAF/extents; Agent 1 owns enforcement.

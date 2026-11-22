# Known C+ compiler gaps observed by Agent 2 (report only — no compiler edits)

These are findings from Agent 2's runtime/test work. They are recorded for Agent 1
(the compiler owner). Agent 2 does not modify `compiler/**` or `runtime/**`.

## 1. Heap/pointer buffer overflow not caught (user repro)

Shape (exactly as reported, adapted to compile):

```c
int *ptr = malloc(1);
for (int x = 0; x < 10; x++) ptr[x] = x + 10;   /* 40 bytes into 1-byte heap */
```

Evidence (`build/c+ build` on `tests/cplus_repro/oob_heap.c`):

| Variant | Result |
|---|---|
| heap `malloc(1)` + `ptr[x]` (0..9) | compiles (exit 0); only a leak **warning**; runs and prints `ptr=10`, silently corrupting the heap. **No `CPLUS-BOUNDS`/`CPLUS-SAFE-BOUNDS`.** |
| local `int buf[1]; buf[x]` (0..9), direct subscript | **static `error: [CPLUS-BOUNDS] out-of-bounds access`**, exit 1, no binary |
| local `int buf[1]` written via a pointer parameter | compiles; runs → UBSan BUS/SIGABRT (incidental crash), no directed bounds diagnostic |

Root cause: the current bounds pass (`compiler/Sema/StaticBoundsCheck.cpp`) operates
at token level within one function and only sees an array's length when the
subscript is written directly on a same-function array declaration. There is no
interprocedural / pointer-extent / heap-capacity analysis, and the safe build
links UBSan `bounds` (which cannot track pointer parameters) without a heap
allocator shadow (ASan). The `CPLUS-SAFE-BOUNDS` tag appears only as contract
metadata for `memcpy`/`memmove`/`memset`.

Needed from Agent 1: interprocedural bounds/ownership propagation and/or a
heap-capacity-aware check so `ptr[x]` on a `malloc(1)` destination is rejected
(or `runtime/**` allocator metadata + a compiled read/write guard fires).

## 2. Other static-analysis false positives seen on owned test code

| Pattern | Reported as | Truth |
|---|---|---|
| `calloc(8, 16)` then `p[127]` | `[CPLUS-BOUNDS] ... heap allocation 'zeroed' of 8 bytes` | `calloc(8,16)` is 128 bytes; pass mis-sizes 2-arg calloc by its first arg |
| `free(p); p = calloc(...); p[i]` | `[CPLUS-LIFETIME] use-after-free: subscript of released pointer` + `[CPLUS-OWNER] double free of 'p'` | reassignment after free is not tracked; false positive |

Worked around in `tests/crt/alloc.c` (distinct buffers/names) so the corpus stays
green; the compiler should handle these correctly.

## Runtime mitigation available today (Agent 2)

`tests/crt/adversarial/test_heap_oob_mitigation.c` shows the **same** bug, written
with the checked API (capacity-tracked allocation + bounded write), is caught:

- `malloc_s(&p, 1)` then `cplus_memcpy_s(p, 1, src, sizeof src)` → non-zero error
  (heap not corrupted); a correct 1-byte write succeeds.
- `cplus_memcpy_s(buf, 1, "0123456789", 10)` → rejected.
- `cplus_memset_s(buf, 1, 0, 10)` → rejected.

This is a *runtime* mitigation for callers that use the checked API; it does not
make raw `ptr[x]` on a `malloc(1)` destination safe.

# EDGE100 io -- results

```
EDGE100 io 62 279 279 0
```

Compiler used: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build-verify-1123/c+`

| Header | APIs | Cases | Passed | Failed |
|--------|------|-------|--------|--------|
| io.hp | 7 | 51 | 51 | 0 |
| file.hp | 13 | 59 | 59 | 0 |
| fs.hp | 8 | 43 | 43 | 0 |
| json.hp | 34 | 126 | 126 | 0 |
| **total** | **62** | **279** | **279** | **0** |

## Failed cases

None. Every case matched the documented contract with intact canaries.

## Method

- One `fork()` per (API, case); the child writes a result struct over a pipe.
- `alarm(4)` watchdog in each child; whole run bounded by `timeout 60`.
- 0xA5 canaries surround every caller buffer; overruns are reported as FAIL.
- Allocation failure injected via compile-time `realloc`/`tmpfile` stubs; yyjson-internal allocations are guarded by length/depth pre-checks.
- Depth budgets exercised at 16384 (within) and 16385 (one over).
- The runner prefers `build/c+` and falls back to another sibling build of the same compiler if the primary cannot compile the harness.
- stdout/stderr of the harness are in `out/stdout.txt` / `out/stderr.log`.

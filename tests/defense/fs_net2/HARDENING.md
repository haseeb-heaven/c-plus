# Agent 2 Phase 5-7 — Filesystem / Network hardening

**Finding F-H1 (genuine, FIXED): `http_sink_write` overflow branches wrote out of bounds.**

Two defects in the original `stdlib/http.hp` sink:
1. When `used == size`, the bounds-refusal branch wrote `buf[used] = '\0'` at
   index `size` — one byte past the destination.
2. When `used == SIZE_MAX` (corrupt/hostile bookkeeping),
   `room = size - used - 1` wrapped to a huge value and `memcpy` wrote before
   the buffer.

The red-team (tests/redteam/json_net) measured 124 genuine CORRUPT cases from
these two paths. Reproducers: `_sink_repro.cp`, `_crlf_repro.cp`.

**Fix** (in `stdlib/http.hp`, landed in commit `55694b5`):
- Refuse immediately when `buf == NULL || used >= size`: set `overflow = 1`,
  return 0, and touch no memory.
- In the remaining overflow branch compute `room = size - used - 1` only after
  proving `used < size`; NUL-terminate at `buf[used]` which is then always
  in range.

**Before/after (agent harness, `sh tests/defense/fs_net2/run.sh`):**

```
BEFORE FS_NET2 1200 944 256 0 0 (exit 1)
AFTER  FS_NET2 1200 1200 0 0 0 (exit 0)
ABI_SYMBOLS identical
PASS fs_net2: F-H1 reproduced before (256 failures), fixed after (0)
```

Red-team confirmation: `REDTEAM json_net 5600 1842→1966 0 124→0 0`.

**Documented non-defect:** `http_headers::add` copies CRLF-bearing header
lines verbatim. No syntax validation is promised by the header contract, so
this is a defined behavior, not a refusal violation (80 DEFINED_COPY cases).

**OPEN policy item (not fixed here, owned by Agent F scope):** `json_parse_file`
calls `yyjson_read_file` directly and does not apply the lexical depth budget
that the `json()` constructor applies. The header's "unconditional budget"
wording is inaccurate; a 20,000-deep file parses successfully. Tracked as
`R-JN-4` in `docs/security/RED_BLUE_BATTLE_FINAL.md`.

**ABI:** no signature, symbol, or layout change (`ABI_SYMBOLS identical`;
fingerprint MATCH, 69/69 compat, 70/70 layouts). Scope: macOS arm64 / Apple
clang 16 only. Evidence for tested inputs, not a proof of full safety.

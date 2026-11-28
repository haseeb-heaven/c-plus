# RED-TEAM agent 5 — JSON / net / http / vendored `cplus_v_*` findings

Branch: `agent/modern-stdlib`. Targets: `stdlib/json.hp`, `stdlib/net.hp`,
`stdlib/http.hp`, and all 26 `cplus_v_*` symbols in
`build/safe/libcplus_safecrt.a` (verified with `nm -g`, 26 names).

All JSON and net/http work is offline. Every case runs in a forked child with
an alarm watchdog and canary guards. Fixed PRNG seed
`0x5EED5005A5A50005` (C+) / `0xC0FFEE5AF10D5EED` (vendored), per-case Weyl
increment.

## Summary

```
REDTEAM json_net 5600 1842 0 124 0
```

| Part | Cases | REFUSED | CRASH | CORRUPT | UNEXPECTED_OK |
|---|---|---|---|---|---|
| json/net/http (C+) | 3000 | 1454 | 0 | 124 | 0 |
| vendored `cplus_v_*` (C) | 2600 | 388 | 0 | 0 | 0 |
| **total** | **5600** | **1842** | **0** | **124** | **0** |

Counts are from the combined run (`sh tests/redteam/json_net/redteam_run.sh`).
Per-case evidence: `results/scenarios.log` (5600 lines, gitignored),
SHA-256 digest in `results/digest.sha256`, per-part FNV digests in stdout.

## Genuine finding 1 — `http_sink_write` writes one byte past the buffer when `used == size` (CORRUPT)

**Severity:** medium. **Result token:** `CORRUPT`, 24 cases (names `snull_*`).

`stdlib/http.hp:118-127`. The bounds-refusal branch is entered when
`sink->used >= sink->size`. It computes `room = 0` and then unconditionally
executes `sink->buf[sink->used] = '\0'`. When `used == size` (a plausible
caller-supplied "buffer is full" state), this writes the NUL at index `size`,
one byte **past** the caller's `[0, size-1]` buffer.

The header comment at `http.hp:114-116` promises the opposite:
"`used` never exceeds `size - 1`, so the subtraction cannot wrap, and an
oversized chunk cannot wrap the sum either." The implementation does not
enforce that invariant for caller-initialized sinks.

Exact reproducer (`tests/redteam/json_net/_sink_repro.cp`, case "F1"), with a
64-byte buffer surrounded by 16-byte canary guards filled with `0xA5`:

```
F1 used==size: ret=0 used=64 overflow=1
  g[PRE-1] (byte before buf) = 0xa5 (expect a5)
  buf[0..7] = a5 a5 a5 a5 a5 a5 a5 a5
  buf[CAP-1] = 0xa5  g[PRE+CAP] (post-guard) = 0x00
```

`g[PRE+CAP]` is the first byte **after** the 64-byte buffer; it was `0xA5`
before the call and is `0x00` after it — the stray NUL terminator.

## Genuine finding 2 — `http_sink_write` underflows when `used == SIZE_MAX`, writing before the buffer (CORRUPT)

**Severity:** high (arbitrary-offset OOB write driven by a corrupted `used`).
**Result token:** `CORRUPT`, 100 cases (names `spoison_*`).

Same bounds-refusal branch. With `used = SIZE_MAX` and `size = 64`:

* `room = sink->size > sink->used + 1 ? ... : 0` — `sink->used + 1` wraps to 0,
  so `64 > 0` is true and `room = 64 - SIZE_MAX - 1 = 64` (mod 2^64) — a
  huge value.
* `memcpy(sink->buf + sink->used, ptr, room)` — `buf + SIZE_MAX` wraps to
  `buf - 1`, and 64 bytes are copied starting one byte **before** the buffer.
* `sink->used += room` wraps to 63; `sink->buf[63] = '\0'`.

Exact reproducer (`_sink_repro.cp`, case "F2"):

```
F2 used==SIZE_MAX: ret=0 used=63 overflow=1
  g[PRE-1] (byte before buf) = 0x42 (expect a5)
  buf[0..7] = 42 42 42 42 42 42 42 a5
  buf[CAP-1] = 0x00  g[PRE+CAP] (post-guard) = 0xa5
```

Seven `0x42` ('B') bytes land before `buf` and inside it; the guard byte
`g[PRE-1]` is destroyed. This is a genuine out-of-bounds write, not a
refusal — the function returns 0 and sets `overflow = 1`, but memory outside
the sink has already been modified.

## Documented finding 3 — `http_headers::add` copies CRLF/bare-CR/bare-LF verbatim

**Severity:** policy/documentation. **Result token:** `DEFINED_COPY`, 80 cases
(names `hdrline_*` with `mode` 1/2/3).

`stdlib/http.hp:58-70` checks only `line == NULL`. Any other bytes — including
`\r\n`, bare `\r`, bare `\n`, and control bytes — are passed verbatim to
`curl_slist_append`, which copies the string. No refusal is promised by the
header (it documents "Copies the line, so the caller keeps ownership of its
string"), so this is **not** counted as CORRUPT/UNEXPECTED_OK, but it is
reported as a documented behavior: callers must sanitize header lines
themselves before passing them to this API.

Exact reproducer (`tests/redteam/json_net/_crlf_repro.cp`):

```
rc=0 valid=1 ok=1 native=0x...
stored_bytes: 58 2d 41 ... 6f 6b 0d 0a 58 2d 49 ... 65 73
strcmp_identical=1
```

The `0d 0a` (`\r\n`) between `X-Adversarial: ok` and `X-Injected: yes` is
preserved in the list node.

## Documented finding 4 — `json_parse_file` bypasses the JSON depth/length budget

**Severity:** policy/documentation. **Not counted** in CORRUPT/UNEXPECTED_OK
(no refusal is promised by `json_parse_file` itself, but the header's budget
comment is unconditional).

`json.hp:34-38` states: "json.hp therefore enforces an equivalent depth/length
budget itself, before yyjson allocates anything." `json_input_within_budget`
is called by the `json` constructor and by `json_parse_budget`, but
`json_parse_file` (`json.hp:300-317`) calls `yyjson_read_file` directly with no
budget check. A file with 20000 nested arrays is accepted:

```
json_parse_file(20000-deep): valid=1 failed=0
```

Reproducer: `tests/redteam/json_net/_file_repro.cp` (writes
`results/deep.json`). A hostile-input deployment that tightens
`CPLUS_JSON_MAX_DEPTH` will not be protected on the file path.

## No findings in the remaining targets

* **JSON depth/length budgets** (`json_input_within_budget`,
  `json_parse_budget`, `json` ctor): all depth 20000+, `SIZE_MAX`/`SIZE_MAX-8`
  lengths, truncated/malformed UTF-8, escaped-quote trickery, null pointers,
  zero budgets, huge array indices, and non-finite builder values produced
  REFUSED or the expected defined result. No crash, corruption, or
  unexpected acceptance of over-budget input.
* **`net.hp`**: invalid/borrowed descriptors (-1, 0, 1, 2, 999999), null
  buffers, zero capacity, closed-port loopback writes, and synthetic HTTP
  byte streams over AF_UNIX socketpairs all produced defined refusals with
  intact guards.
* **All 26 `cplus_v_*` vendored functions** (names verified with `nm`): 2600
  cases covering one-over lengths, `RSIZE_MAX_STR+1`, `SIZE_MAX`, embedded
  NUL, non-ASCII and control bytes, null dest/src/out, and the silent
  constraint handler. Zero crashes, hangs, guard corruptions or unexpected
  successes where refusal is promised.

## Reproduce

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/redteam/json_net/redteam_run.sh
# prints: REDTEAM json_net 5600 1842 0 124 0
```

Build/run commands for individual reproducers are in each `_*.cp` source
header comment. `results/` is gitignored (`.gitignore` inside it).

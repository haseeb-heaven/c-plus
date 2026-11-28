# NOTES — `FUZZ10K net_http` (10,000 deterministic forked offline scenarios)

Branch `agent/modern-stdlib`. All created/modified files are under
`tests/fuzz10k/net_http/`. No header, source, `.gitignore` at the repo root, or
file outside that directory was edited. Nothing was staged or committed.
Generated artifacts live in `results/`, ignored by `results/.gitignore`
(`*` plus `!.gitignore`).

**This is evidence, not proof.** The run demonstrates 10,000 fixed-seed
executions of defined API contracts on this host (libcurl 8.7.1), in forked
subprocesses with per-child watchdogs and sentinel checks. It is not a formal
proof of the headers, the C+ lowering, or libcurl, and it makes no claim about
live-game/live-network behavior. Every scenario is offline: libcurl's
in-process URL parser, the header's bounded response sink, curl slists,
invalid/borrowed descriptors and `AF_UNIX` socketpairs carrying synthetic HTTP
bytes. There is no DNS, proxy, external host, or listening socket.

## 1. Why `--default` is required (exact strict-build error)

`http.hp` cannot be lowered with `--strict`. The strict checker rejects the
uneditable header at `stdlib/http.hp:106` (`SIZE_MAX / size`, guarded in source
by `size != 0 &&` but the checker cannot exclude the zero divisor) and at
`stdlib/http.hp:230` (unverifiable buffer extent for `buf[0]`). Reproduced here
with a minimal probe (`_strict_probe.cp`):

```
$ timeout 120 ./build/c+ build tests/fuzz10k/net_http/_strict_probe.cp --strict --sanitize=none -lcurl -o tests/fuzz10k/net_http/results/_strict_probe
STRICT_RC=1
./stdlib/http.hp:106:30: error: [CPLUS-ARITH] division by zero cannot be excluded
      }
./stdlib/http.hp:230:9: error: [CPLUS-FFI] pointer parameter 'buf' has unverifiable extent; subscript cannot be proven safe
          buf[0] = '\0';
          ^~~
```

Full strict output is in `results/strict_probe.log`. The offending source is:

```c
    if (size != 0 && nmemb > SIZE_MAX / size) {   /* stdlib/http.hp:106 */
```

The same file builds in `--default` mode where these are warnings (see
`results/default_probe.log`, `results/build.log`). This matches the repository's
own documentation (`tests/stdlib/method_tests/net_http/RUN_NOTES.md:142-173`,
`tests/adversarial_1000/net_http/NOTES.md:14-51`). Per the file-ownership rule
the header was not edited, so the build uses the required mode:

```
./build/c+ build tests/fuzz10k/net_http/fuzz10k_net_http.cp --default --sanitize=none -lcurl -o <bin>
```

The `clang: warning: -lcurl: 'linker' input unused [-Wunused-command-line-argument]`
message is benign: the binary resolves curl symbols (`otool -L` shows
`/usr/lib/libcurl.4.dylib`) and runs.

## 2. Exact commands and results

All commands run from the repository root. Wrapper:
`sh tests/fuzz10k/net_http/run.sh` (it reproduces the commands below, applies
`timeout 180` to the build and `timeout 600` to the run, and writes the result
artifacts).

### 2.1 Build

```
$ timeout 180 ./build/c+ build tests/fuzz10k/net_http/fuzz10k_net_http.cp --default --sanitize=none -lcurl -o tests/fuzz10k/net_http/results/fuzz10k_net_http
BUILD_RC=0
```

Diagnostics in `results/build.log` are warnings only (the two documented
`http.hp` warnings, the benign `-lcurl` warning, and `[CPLUS-FFI]`/remainder
warnings in this test's own helper functions). No errors.

### 2.2 Run

```
$ timeout 600 tests/fuzz10k/net_http/results/fuzz10k_net_http
FUZZ10K net_http 10000 0 0 0 6027
FUZZ10K_DIGEST 9be8e7b3f9677054
RUN_RC=0
```

Runtime ~18-23 s (well inside `timeout 600`). The required summary line is
exactly: `FUZZ10K net_http <scenarios> <crashes> <hangs> <corruptions> <refusals>`
= `FUZZ10K net_http 10000 0 0 0 6027`. Exit 0 means no crash, hang, corruption,
or defined-result mismatch. `results/stderr.log` contains only the detail line
`FUZZ10K detail sentinel_ok=10000 defined_result_mismatch=0`.

### 2.3 Offline control and forked isolation

The parent first forks a control child that constructs `http` (curl global
init) and calls `curl_url_set` on `http://127.0.0.1:9/control` (offline parse
only; no transfer). A failed control aborts with exit 4 before any scenario.

Each of the 10,000 scenarios then runs in its own `fork()`ed subprocess. The
child sets an alarm(10) watchdog, writes exactly one result line to a pipe, and
exits. The parent reads that line and uses `waitpid()`: a child dying by a
non-alarm signal is a **crash**, a SIGALRM/SIGKILL death is a **hang**, and a
missing/malformed result line is a failure. A corrupted sentinel or a
defined-result mismatch increments **corruption**. This isolates any latent
bad access so it cannot take down the whole run.

### 2.4 Determinism and log integrity

Two consecutive runs of the same binary produced a byte-identical
`scenarios.log` (identical SHA-256; `cmp` found no differing bytes) and
byte-identical stdout:

```
$ shasum -a 256 tests/fuzz10k/net_http/results/scenarios.log
58dca5e09cbf0edcc2dcdf3c0205719c86d80bdc56e47821e611ee675ca53a46  tests/fuzz10k/net_http/results/scenarios.log
```

Fixed seed `0x243F6A8885A308D3ULL` with xorshift64*; each id gets a distinct
derived seed `seed XOR ((id+1) * 0x9E3779B97F4A7C15ULL)`. No clock, PID, address,
environment value or external entropy enters a scenario or its expected result.

Log integrity checks (`scenarios.log`): 10,000 lines; 10,000 distinct ids
(`00000`..`09999`); 10,000 distinct case labels; every line has the exact 8-field
form `S <id> <api> <case> <result> <sentinel_ok> <ok> <refusal>`
(`awk` bad-lines = 0); `sentinel_ok=1` on all 10,000, `ok=1` on all 10,000;
`refusal=1` on 6,027 and `refusal=0` on 3,973.

## 3. Scenario families (10,000 total, distinct ids)

| IDs | API (`<api>` column) | Family | What is asserted |
|---|---|---|---|
| 00000-00999 | `http_sink_write` | `zero_cap_*` (1000) | `size == 0` (incl. NULL buf, huge `size*nmemb`): return 0, no writes, all sentinel bytes intact |
| 01000-01999 | `http_sink_write` / `http_client.get` | `null_*` (500), `client_arg_*` (500) | NULL sink/buf/ptr and NULL URL/buffer/size 0: `STDLIB_FAIL`, error set, buffer unchanged |
| 02000-02999 | `http_sink_write` | `cap_*` (1000) | capacities 0,1,2,3,7,8,...,4095,4096 plus PRNG sizes; write lengths exact-fit/one-over/1/remaining/4097/random. Return, `used`, `overflow`, copied bytes, NUL placement and trailing sentinel checked against an independent model |
| 03000-03499 | `http_sink_write` | `mulovf_*` (500) | `size*nmemb` overflow (`SIZE_MAX`, 2^62) sets `overflow=1`, writes nothing; zero products are a defined no-op |
| 03500-04999 | `curl_url_set` | `urlbad_*` (1500) | 17 malformed URL templates + random-byte bodies, random segments with bad ports, unterminated bracketed literals, injected control bytes, all with a forced leading space (probe-confirmed refusal): parser must refuse, scratch sentinel intact |
| 05000-05499 | `curl_url_set` | `urllong_*` (500) | URLs 8,000,001..8,001,000 bytes: parse refused, no scratch overrun |
| 05500-06499 | `http_headers.add` | `hdrnull_*` (136), `hdrline_*` (864) | NULL line refused; CRLF/bare-CR/bare-LF/control/empty/oversized (up to ~8 KB) lines copied verbatim, list integrity and sentinel intact |
| 06500-07299 | `connection.read`/`write`, `listener.bound_port`/`accept` | `netbad_*` (800) | borrowed fd -1, 0, 1, 2, 999999 and invalid listeners: defined refusal with correct error category, inert handle, no buffer write |
| 07300-08299 | `connection.read` | `trunc_*` (1000) | AF_UNIX socketpair carrying truncated HTTP messages; bounded read count, sentinel after `n`, defined framing classification |
| 08300-09299 | `connection.read` | `chunk_*` (1000) | Same, chunked-framing edge cases (bad hex, truncated data, missing terminal chunk, bad CRLF, trailers, trailing garbage) |
| 09300-09999 | `connection.read` | `method_*` (700) | Random invalid method tokens (punctuation + high bytes) in a request line; bounded read, sentinel intact, defined framing classification |

`curl_url_set` is the libcurl URL parser that `http.hp` delegates URL
validation to through `CURLOPT_URL` (`stdlib/http.hp:232`); `http.hp` exposes no
standalone URL-parse wrapper, so the scenarios call this offline parser directly
with `<curl/urlapi.h>`. F2's `http_client.get/request` cases exercise the
header's own argument-refusal path before any transfer can start. The HTTP
framing classifier (`classify_http`) is a small deterministic model in the test,
not a libcurl parser; its expected tokens are fixed by the payload templates.

## 4. Probing and debugging evidence

Prerequisite probes confirmed, before the final run:

* Malformed URLs refused by `curl_url_set(CURLUPART_URL, ...)`:
  `""`→E3, `"://"`→E27, `"http://"`→E14, `"http:// "`→E3,
  `"http://[::1"`→E22, `"http://example.com:abc/"`→E4, and any string with a
  leading space→E3 (verified with a standalone ctypes probe against
  `/usr/lib/libcurl.4.dylib`). Note `CURLUPART_URL` is enum value **0**.
* Overlong URL threshold (libcurl 8.7.1): 8,000,001 bytes and above refused.
  F6 uses 8,000,001..8,001,000.
* `http_sink_write` sink semantics: `size == 0` returns before any write and
  leaves `overflow` untouched; an exact `size`-byte payload is *not* accepted
  (one byte reserved for the NUL), yielding a bounds refusal with `used=size-1`.
  The independent model in F3 was corrected to match both facts.
* `http_headers.add(NULL)` returns `STDLIB_FAIL`; oversized/control-bearing
  lines are copied verbatim (documented limitation, see below).
* `socketpair(AF_UNIX, SOCK_STREAM)` reads return exactly
  `min(capacity, available)` with remaining sentinel bytes untouched.

The final harness retains diagnostic `fprintf`s that fire only on a
`ACCEPTED_URL` or `CAP_MISMATCH` failure; none fired in the final run, so
`stderr.log` is clean.

## 5. Scope, findings and limitations

* **Zero-length buffers — refused, no corruption.** F1 and the NULL-arg cases
  all return the documented failure value; every guard byte stays `0xA5`.
* **Overlong URL — refused, no corruption.** F6 uses URLs above libcurl's
  8,000,000-byte input limit; parse is refused, guard bytes intact.
* **Malformed header injection — defined result, no corruption (honest
  finding).** `http_headers::add` and `http_client::set_headers`
  (`stdlib/http.hp:65-79,177-204`) do **not** validate header syntax: a line
  with `\r\n`, bare CR/LF, or control bytes is copied verbatim by
  `curl_slist_append`. F7 asserts the *defined parse result* (`STDLIB_OK`,
  verbatim copy, single-entry list, sentinel intact), not a refusal the header
  does not implement. This is evidence of the actual contract and a documented
  limitation; the harness would fail on cross-entry bleed or string overrun.
* The sink's documented NUL reservation is honored; scenarios keep
  `sink.used < sink.size`. The header's `used >= size` state (which would write
  a terminator at `buf[used]`, past the caller capacity) is a precondition
  violation and is deliberately not exercised.
* `sentinel_ok` means all `0xA5` guard bytes before/after the caller region and
  all bytes beyond the API's documented write window stayed unchanged.
  `refused` counts cases (6,027) whose expected outcome is an explicit
  API/parser rejection; the other 3,973 are defined-result cases.

## 6. Files

* `fuzz10k_net_http.cp` — the 10,000-scenario forked harness (single TU).
* `run.sh` — build/run/digest wrapper with `timeout 180`/`timeout 600`.
* `_strict_probe.cp` — minimal probe isolating the documented strict error.
* `results/.gitignore` — ignores all generated artifacts in `results/`.
* `results/` (gitignored) — `scenarios.log`, `stdout.log`, `stderr.log`,
  `build.log`, `digest.sha256`, `strict_probe.log`, `default_probe.log`, the
  two-run comparison copies, and the built binary.

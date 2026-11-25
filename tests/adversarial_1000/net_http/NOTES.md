# NOTES — `ADV1000 net_http` (1000 deterministic adversarial scenarios)

Branch `agent/modern-stdlib`. All created/modified files are under
`tests/adversarial_1000/net_http/`. No header, source, `.gitignore` at the repo
root, or file outside that directory was edited. Nothing was staged or
committed. Generated artifacts live in `results/`, which is ignored by the
`results/.gitignore` file inside this directory (`*` plus `!.gitignore`).

**This is evidence, not proof.** The run demonstrates 1000 fixed-seed
executions of defined API contracts on this host and libcurl 8.7.1. It is not a
formal proof of the headers, the C+ lowering, or libcurl, and it makes no claim
about live-game/live-network behavior.

## 1. Why `--default` is required for `http.hp`

The exact reason was already recorded by the repository's own method test,
`tests/stdlib/method_tests/net_http/methods_http.cp:3-8`:

> BUILD NOTE: this file cannot be lowered with --strict because the header
> itself contains `SIZE_MAX / size` at stdlib/http.hp:106, which the C+
> strict checker rejects as "[CPLUS-ARITH] division by zero cannot be
> excluded" even though the source guards it with `size != 0 &&`.  Headers are
> not editable here, so the build uses --default --sanitize=none -lcurl
> (the same mode the repository's own tests/stdlib/run_stdlib_tests.sh uses).

Corroborated in `tests/stdlib/method_tests/net_http/RUN_NOTES.md:142-172`:
`./build/c+ build tests/stdlib/http_test.cp --strict --sanitize=none` exits 1
with `./stdlib/http.hp:106:30: error: [CPLUS-ARITH] division by zero cannot be
excluded`; the same source builds in `--default` mode where the diagnostic is a
warning. The offending line is `stdlib/http.hp:106`:
`if (size != 0 && nmemb > SIZE_MAX / size) {`.

Reproduced here (command and result, repository root):

```
$ timeout 120 ./build/c+ build tests/adversarial_1000/net_http/adv1000_net_http.cp --strict --sanitize=none -lcurl -o tests/adversarial_1000/net_http/results/_strict_probe
STRICT_BUILD_RC=1
./stdlib/http.hp:106:30: error: [CPLUS-ARITH] division by zero cannot be excluded
./stdlib/http.hp:230:9: error: [CPLUS-FFI] pointer parameter 'buf' has unverifiable extent; subscript cannot be proven safe
...
```

The required build is therefore (with `--sanitize=none` and `-lcurl`):

```
./build/c+ build tests/adversarial_1000/net_http/adv1000_net_http.cp --default --sanitize=none -lcurl -o <bin>
```

The `clang: warning: -lcurl: 'linker' input unused` message is benign: the
binary resolves curl symbols and runs (the same observation is documented in
`tests/stdlib/method_tests/net_http/RUN_NOTES.md:206-208`).

## 2. Exact commands and results

All commands run from the repository root. Wrapper: `sh
tests/adversarial_1000/net_http/run.sh` (it reproduces the commands below and
writes `results/{build,stdout,stderr,run2}.log`, `results/digest.sha256`).

### 2.1 Build

```
$ timeout 120 ./build/c+ build tests/adversarial_1000/net_http/adv1000_net_http.cp --default --sanitize=none -lcurl -o tests/adversarial_1000/net_http/results/adv1000_net_http
build rc=0
```

Build diagnostics (warnings only, in `results/build.log`): the two documented
`stdlib/http.hp:106` and `stdlib/http.hp:230` warnings, the benign `-lcurl`
warning, and `[CPLUS-FFI] pointer parameter ... unverifiable extent` / remainder
warnings in this test's own sentinel helpers. There are no errors and no
failing cases.

### 2.2 Run

```
$ timeout 120 tests/adversarial_1000/net_http/results/adv1000_net_http
... 1000 `S ...` lines ...
ADV1000 net_http 1000 660 1000 0
ADV1000_DIGEST c0283473327a64ae
run rc=0
```

* `1000` `S` lines written to `results/scenarios.log` (`wc -l` = 1000).
* `1000` distinct ids (`0000`..`0999`) and `1000` distinct case labels
  (`sort -u | wc -l` = 1000 for both fields).
* Every `S` line has exactly six whitespace fields, form
  `S <id> <api> <case> <result> 1` (the last field is `sentinel_ok`);
  `bad_lines=0` under an `awk` format check.
* `refused=660`, `sentinel_ok=1000`, `failures=0`; process exit 0.
* `results/stderr.log` is empty (0 lines) in the final build.

### 2.3 Determinism

Two consecutive runs of the same binary produced byte-identical
`scenarios.log` and stdout (`cmp` exit 0 for both), with the same digests:

```
$ shasum -a 256 tests/adversarial_1000/net_http/results/scenarios.log
df3bb4da9a2e7ea7f18c754449cfa89b77acf919ca62ef9640511c693c9c7bd9  tests/adversarial_1000/net_http/results/scenarios.log
```

The fixed seed is `0x243F6A8885A308D3ULL` with xorshift64*; no clock, PID,
address, environment value or external entropy enters a scenario or its
expected result.

### 2.4 Prerequisite probes (kept as `_probe*.cp`)

```
$ ./build/c+ build tests/adversarial_1000/net_http/_probe.cp --default --sanitize=none -lcurl -o results/_probe
$ ./build/c+ build tests/adversarial_1000/net_http/_probe2.cp --default --sanitize=none -lcurl -o results/_probe2
$ ./build/c+ build tests/adversarial_1000/net_http/_probe3.cp --default --sanitize=none -lcurl -o results/_probe3
```

Evidence used to fix the expected results:

* Malformed URLs rejected by `curl_url_set(..., CURLUPART_URL, ..., 0)`:
  `""`→E3, `"://"`→E27, `"http://"`→E14, `"http:// "`→E3,
  `"http://[::1"`→E22, `"http://example.com:abc/"`→E4,
  `"mailto:user"`→E5. All non-OK codes are treated as refusal; the scenario
  result column records the exact code (`REFUSED_E<n>`).
* Overlong URL threshold (libcurl 8.7.1): lengths 1,000,000 / 4,000,000 /
  7,000,000 / 8,000,000 returned OK; 8,000,001 and above returned E3
  (`CURLUE_MALFORMED_INPUT`). F6 therefore uses 8,000,001..8,001,000 bytes.
* `http_sink_write` sink semantics: an exact `size`-byte payload is *not*
  accepted (one byte is reserved for the NUL), so it yields a bounds refusal
  with `used = size-1` — mirrored in the independent model.
* `http_headers.add(NULL)` returns `STDLIB_FAIL`; an empty line returns OK.
* `socketpair(AF_UNIX, SOCK_STREAM)` reads return exactly `min(capacity,
  available)` with the remaining sentinel bytes untouched.

## 3. Scenario families (100 each, 1000 total)

| IDs | API (`<api>` column) | Family | What is asserted |
|---|---|---|---|
| 0000-0099 | `http_sink_write` | `zero_cap_*` | `size == 0` (incl. NULL buf and huge `size*nmemb`): return 0, no writes, all sentinel bytes intact |
| 0100-0199 | `http_sink_write` / `http_client.get` | `null_*`, `client_arg_*` | NULL sink/buf/ptr and NULL URL / NULL buffer / size 0 / NULL request args: `STDLIB_FAIL`, error set, no write |
| 0200-0299 | `http_sink_write` | `cap_*` | capacities 0,1,2,3,7,8,...,4095,4096 plus PRNG sizes; write lengths exact-fit/one-over/1/remaining/4097/random; return, `used`, `overflow`, copied bytes, NUL placement and trailing sentinel checked against an independent model |
| 0300-0399 | `http_sink_write` | `mulovf_*` | `size*nmemb` overflow (SIZE_MAX, 2^62) sets `overflow=1` and writes nothing; zero products are a defined no-op |
| 0400-0499 | `curl_url_set` | `urlbad_*` | 17 probe-confirmed malformed URL templates, optionally prefixed with space/CR/TAB; parse must be refused; scratch sentinel intact |
| 0500-0599 | `curl_url_set` | `urllong_*` | URLs 8,000,001..8,001,000 bytes: parse refused, no scratch overrun |
| 0600-0699 | `http_headers.add` | `hdrnull_*`, `hdrline_*` | NULL line refused; CRLF/bare-CR/bare-LF/control/empty/oversized (up to ~1800 B) lines copied verbatim with list integrity and sentinel intact |
| 0700-0799 | `connection.read` / `connection.write` / `listener.bound_port` / `listener.accept` | `netbad_*` | borrowed fd -1, 0, 1, 2, 999999 and invalid listeners: defined refusal with correct error category / inert handle, no buffer write |
| 0800-0899 | `connection.read` | `trunc_*` | AF_UNIX socketpair carrying truncated HTTP messages; bounded read count, sentinel after `n`, and a defined framing classification |
| 0900-0999 | `connection.read` | `chunk_*` | Same, for chunked-framing edge cases (bad hex, truncated data, missing terminal chunk, bad CRLF, trailers, trailing garbage) |

`curl_url_set` is the *libcurl URL parser* that `http.hp` delegates URL
validation to through `CURLOPT_URL` (`stdlib/http.hp:232`); `http.hp` exposes no
standalone URL-parse wrapper, so the scenarios call this offline parser
directly with `<curl/urlapi.h>`. No transfer or DNS lookup is performed. F2's
`http_client.get/request` cases exercise the header's own argument-refusal
path before `curl_easy_perform`.

## 4. The three BREAKING categories

* **Zero-length buffers — refused, no corruption.** F1 (100 cases, `size=0`)
  and the `size == 0` / NULL-buffer `http_client` cases in F2 all return the
  documented failure value; every guard byte remains `0xA5`.
* **Overlong URL — refused, no corruption.** F6 (100 cases) uses URLs above
  libcurl's 8,000,000-byte input limit; `curl_url_set` refuses each one and the
  guard bytes around the URL buffer remain `0xA5`.
* **Malformed header injection — defined result, no corruption (honest
  finding).** `http_headers::add` and `http_client::set_headers` in
  `stdlib/http.hp:65-79,177-204` do **not** validate header syntax: a line
  containing `\r\n`, bare CR/LF or control bytes is copied verbatim by
  `curl_slist_append`. F7 therefore asserts the *defined parse result*
  (`STDLIB_OK`, verbatim copy, single-entry list, sentinel intact) rather than
  claiming a refusal that the header does not implement. This is evidence of
  the actual contract and a documented limitation; the harness would fail on
  any cross-entry bleed, string overrun or sentinel corruption. (`add(NULL)`
  is a genuine refusal and is covered separately in the same family.)

## 5. Scope and limitations

* No live external network: no DNS, proxy, external host, or 127.0.0.1
  listener is used. Socket APIs are `AF_UNIX` socketpairs only; the one URL
  used as an argument in F2 (`http://127.0.0.1:1/offline`) is rejected by local
  argument validation before any transfer can start.
* The HTTP framing classifier is a small deterministic model implemented in
  the test (`classify_http`), not a libcurl parser; its expected tokens are
  fixed by the payload templates, and prefix-truncated reads must yield one of
  the model's defined tokens.
* The sink's documented NUL-terminator reservation is honored: scenarios keep
  `sink.used < sink.size`. The header's `used >= size` state (which writes a
  terminator at `buf[used]` and would overrun the caller's capacity) is a
  precondition violation and is deliberately not exercised, so the harness
  measures the defined contract rather than an out-of-contract state.
* `sentinel_ok` means the `0xA5` guard bytes before/after the caller region and
  all bytes beyond the API's documented write window stayed unchanged.
  `refused` counts cases whose expected outcome is an explicit API/parser
  rejection (660 of 1000); the other 340 are defined-result cases.

## 6. Files

* `adv1000_net_http.cp` — the 1000-scenario harness (single translation unit).
* `run.sh` — build/run/digest wrapper with `timeout 120`.
* `_probe.cp`, `_probe2.cp`, `_probe3.cp` — prerequisite evidence probes.
* `results/.gitignore` — ignores all generated artifacts in `results/`.
* `results/` (gitignored) — `scenarios.log`, `digest.sha256`, `stdout.log`,
  `stderr.log`, `build.log`, `run2.log`, binary.

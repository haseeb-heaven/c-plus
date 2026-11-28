# Safety audit — `stdlib/http.hp` (HTTP via libcurl)

> **LABEL: STATIC SOURCE INSPECTION ONLY — NOT EXECUTED.** No test runner, compiler, or
> host process was run to produce this document. All statements derive from reading
> `stdlib/http.hp` and the static inventory `docs/stdlib/REGRESSION_INVENTORY.md`. No
> behavior is claimed to be verified by execution. See §7.

## 1. Provenance and method

- Header inspected: `stdlib/http.hp` (298 lines). Scope comment at `stdlib/http.hp:1-6`;
  includes at `stdlib/http.hp:10-14` (curl at line 10).
- Exact counts/unexercised names from `docs/stdlib/REGRESSION_INVENTORY.md`:
  - §3 total **19 public callable entries** (`docs/stdlib/REGRESSION_INVENTORY.md:46,58`).
  - §4 exercised **5 of 19** (`docs/stdlib/REGRESSION_INVENTORY.md:75,87`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:122`.
  - §5 not exercised **14 of 19** (`docs/stdlib/REGRESSION_INVENTORY.md:150,162`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:193`.
- Static, not execution — `docs/stdlib/REGRESSION_INVENTORY.md:6-15,232-234`.

## 2. Public API list with counts

The inventory's 19 entries, with header line numbers:

| Class / group | Entries (header lines) | Count |
|---|---|---|
| `http` | `http` ctor `:20-30`; `valid` `:32-34`; `drop` `:36-41` | 3 |
| free | `http_version` `:44-46` | 1 |
| `http_headers` | `http_headers` ctor `:51-53`; `valid` `:55-57`; `native` `:60-62`; `add` `:65-79`; `drop` `:81-86` | 5 |
| `http_client` | `http_client` ctor `:136-142`; `valid` `:144-146`; `set_timeout` `:148-161`; `follow_redirects` `:163-175`; `set_headers` `:177-204`; `header_lines` `:208-214`; (request omitted); `get` `:261-263`; (post omitted); `status` `:270-276`; `bytes` `:278-284`; `drop` `:286-295` | 10 |
| **Total** | | **19** |

**Parser omissions found by source inspection** (multi-line signatures / internal callback
missed by `entries()`; real public callables):
- `http_sink_write(char *, size_t, size_t, void *)` at `stdlib/http.hp:97-128` — an
  internal libcurl write callback (static, not `static inline`), not a documented public
  entry; its omission is consistent with the inventory's exclusion of helpers.
- `http_client.request(const char *, const char *, char *, size_t, error *)` at
  `stdlib/http.hp:216-259` (signature spans lines 217-218).
- `http_client.post(const char *, const char *, char *, size_t, error *)` at
  `stdlib/http.hp:265-268` (signature spans lines 265-266).

Source inspection therefore sees **21 named functions** (18 documented public callables +
the internal `http_sink_write`), while the audited headline count is the inventory's **19**.

## 3. Public surface safety analysis (STATIC — source inspection only)

- **`http` (process-wide lifetime):** constructor calls `curl_global_init(CURL_GLOBAL_DEFAULT)`,
  failing with `CPLUS_EXIT_RESOURCE` (`:20-30`); `drop` calls `curl_global_cleanup`
  (`:36-41`). Header comment explicitly notes "clients must be dropped before it"
  (`:16`). No enforcement of that ordering; multiple `http` instances would call
  global init/cleanup multiple times.
- **`http_headers`:** constructor sets `list = NULL`; `valid()` is true only for a non-empty
  list (`:51-57`). `add` copies the line via `curl_slist_append` (caller keeps ownership,
  `:64-79`) and maps allocation failure to `CPLUS_EXIT_RESOURCE`. `native()` returns the
  borrowed list (`:60-62`). `drop` frees the whole list (`:81-86`).
- **`http_sink_write` — response sink bounds logic:** validates sink/buf/size (`:101-102`),
  guards `size * nmemb` against `SIZE_MAX` (`:103-106`), returns 0 for zero-byte or NULL
  chunks (`:108-111`). On overflow it copies only the remaining room, null-terminates,
  sets `overflow`, and returns 0 to abort (`:112-123`); otherwise it copies `bytes`,
  null-terminates, and returns `bytes` (`:124-127`). The comment claims `used` never
  exceeds `size - 1` (`:112-113`), consistent with `buf[0] = '\0'` and the `size - used - 1`
  bound. This is the module's main memory-safety defense, and it is **not deep-tested**.
- **`http_client` construction / `valid`:** `curl_easy_init` with `CPLUS_EXIT_RESOURCE` on
  failure (`:136-142`).
- **`set_timeout`:** validates handle and `seconds >= 0` (`:148-161`). `0` seconds is
  accepted and handed to `CURLOPT_TIMEOUT` (libcurl treats 0 as no timeout) — no local
  upper bound.
- **`follow_redirects`:** maps `enabled` to 1L/0L (`:163-175`); no URL-scheme restriction
  on where redirects may lead (libcurl defaults apply).
- **`set_headers`:** deep-copies the caller's `curl_slist` into `header_copy` (`:177-204`),
  frees the previous copy (`:195-196`), then sets `CURLOPT_HTTPHEADER`. On copy failure it
  frees the partial copy and returns `CPLUS_EXIT_RESOURCE` (`:187-192`). If the final
  setopt fails after the copy was installed, the function returns FAIL but leaves
  `header_copy` installed (`:198-202`) — static observation of a potential state mismatch,
  not an executed result.
- **`header_lines`:** counts the owned copy (`:206-214`), useful as a copy-vs-borrow proof.
- **`request` (shared body of get/post):** validates handle/url/buf/size (`:222-225`),
  zeroes `buf` and the sink (`:226-230`), sets URL/write callback/write data and chooses
  GET or POST with `CURLOPT_POSTFIELDSIZE = (long)strlen(body)` (`:232-240`). Note the cast
  of `size_t` to `long` — very large bodies could truncate; no length check in the header.
  Clears `CURLOPT_WRITEDATA` after success/failure (`:241,247`), maps CURLcode failure to
  `CPLUS_EXIT_IO` (`:248-252`), and maps `sink.overflow` to a bounds category with
  `CPLUS_EXIT_IO` (`:253-257`). The sink is a stack local whose address is handed to
  libcurl only during `curl_easy_perform`; the WRITE data is cleared afterwards.
- **`get`/`post`:** thin wrappers over `request` (`:261-268`, C+ method lowering
  `http_client_request` at `:262,267`). `status`/`bytes` read `CURLINFO_RESPONSE_CODE` and
  `CURLINFO_SIZE_DOWNLOAD_T` (`:270-284`) with no error check on `curl_easy_getinfo` —
  failure silently yields the initialized 0. `drop` cleans up handle and header copy
  (`:286-295`).

All above is read from source; none of it has been exercised here.

## 4. External dependency requirements

- **libcurl** (curl licence): `#include <curl/curl.h>` at `stdlib/http.hp:10`; "Link with
  pkg-config libcurl" at `stdlib/http.hp:5`. Uses `curl_global_init/cleanup`,
  `curl_version`, `curl_easy_init/cleanup/setopt/perform/getinfo`, `curl_slist_append`,
  `curl_slist_free_all`, `curl_easy_strerror`, `CURLcode`, `CURLOPT_*`, `CURLINFO_*`.
- **C+ runtime / `cplus/cplus.h`**, `stdlib/error.hp`, and `<stdint.h>`/`<string.h>` at
  `stdlib/http.hp:11-14`.
- **Not a dependency of this module:** libuv, SDL3, stb, ffmpeg, miniaudio. ffmpeg is
  `stdlib/video.hp`.
- **Host availability (static dependency query only, not a test):**
  `pkg-config --modversion libcurl` returned `8.7.1`. An existing compile-stage log
  `build/stdlib-tests/http_test.build.log` shows `-lcurl` being passed (with "linker input
  unused" warnings, i.e. compile-only). The shallow `http_test.cp` additionally includes
  POSIX socket headers (`sys/socket.h`, `arpa/inet.h`, etc.) — it evidently stands up a
  local server for its own use, but that test is excluded from the deep corpus
  (`docs/stdlib/REGRESSION_INVENTORY.md:12,230`). Dependency presence is established;
  no executed behavior is implied.

## 5. APIs lacking any deep test

Per inventory §5 (`docs/stdlib/REGRESSION_INVENTORY.md:162,193`), the **14** unexercised
entries are: `http.http`, `http.drop`, `http_version`, `http_headers.http_headers`,
`http_headers.add`, `http_headers.drop`, `http_client.http_client`,
`http_client.set_timeout`, `http_client.follow_redirects`, `http_client.set_headers`,
`http_client.header_lines`, `http_client.status`, `http_client.bytes`, `http_client.drop`.

Additionally, source inspection finds these absent from the inventory and uncredited:
`http_sink_write` (internal, `:97-128`), `http_client.request` (`:216-259`), and
`http_client.post` (`:265-268`).

**Stronger source-inspection caveat:** no deep-corpus file includes `stdlib/http.hp`, and
grep finds no deep file containing `http_client`, `http_headers`, or `http_version` tokens.
The 5 "exercised" credits (`docs/stdlib/REGRESSION_INVENTORY.md:122`: `http.valid`,
`http_headers.valid`, `http_headers.native`, `http_client.valid`, `http_client.get`) are
bare-name lexical collisions on generic `valid(`, `native(`, `get(` tokens in unrelated
deep tests — the D2 upward bias at `docs/stdlib/REGRESSION_INVENTORY.md:222`. In
particular, the response-sink overflow logic (`http_sink_write`), `set_headers` copy
semantics, and the GET/POST request body (`request`/`post`, which are not even counted by
the inventory) have **no** deep test. The shallow `http_test.cp` and demo exist but are
excluded as evidence (`docs/stdlib/REGRESSION_INVENTORY.md:12,230`).

## 6. Environment blockers for execution on this host

- **No deep test to execute:** no `deep_*.cp` includes this header.
- **libcurl is installed** (`8.7.1`, §4), so a missing library is not the blocker.
- **Network / endpoint requirement:** real `get`/`post` calls (`stdlib/http.hp:261-268`)
  need a reachable HTTP endpoint and, for https, TLS certificate store and DNS. An
  offline or restricted host blocks genuine requests. The module offers no mock/inject
  transport hook; the shallow `http_test.cp` works around this by including POSIX socket
  headers to host a local server — a pattern not present in the deep corpus.
- **Timeout semantics:** `set_timeout` accepts `seconds >= 0` (`:148-161`); a test relying
  on a default timeout cannot assume one is set (0 means no libcurl timeout). No env
  configuration in the header supplies a default.
- **Process-wide init:** `http` must outlive all clients (`:16,20-41`); executing a test
  requires respecting that ordering manually.

I did not run any test or host process that executes the module; no executed behavior is
claimed.

## 7. What cannot be guaranteed

- Static inspection cannot guarantee `curl_global_init`, `curl_easy_init`, any
  `curl_easy_setopt`/`perform`, or redirect/TLS handling succeeds on this or any host.
- It cannot guarantee the response-sink overflow math (`:112-127`) is correct for all
  chunk sizes — it is purely read, not executed or fuzzed.
- It cannot guarantee the `(long)strlen(body)` cast (`:239-240`) is safe for very large
  bodies, or that a failed final setopt in `set_headers` leaves consistent state
  (`:195-202`).
- It cannot guarantee `status`/`bytes` are meaningful when `curl_easy_getinfo` fails
  (`:270-284` — return value ignored), or that process-wide `http` is correctly ordered
  against clients (`:16`).
- It cannot guarantee any "exercised" credit reflects a real call: all 5 are lexical
  collisions and no deep test includes the header
  (`docs/stdlib/REGRESSION_INVENTORY.md:222,227,234`).
- No execution-based safety, regression, or runtime-success claim is made for this module.

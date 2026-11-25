# stdlib attack surface — Phase 1 inventory

Status: **point-in-time static inventory** (branch `agent/modern-stdlib`, 2026-11-23).
This document maps the *public* attack surface of the checked C+ standard library. It
does **not** measure executed coverage, prove safety, or claim any live-game/runtime
result. Every count below is derived by enumeration from the sources named in this
document; no percentages or estimated counts are used.

## 0. Scope, method, and evidence rules

### 0.1 Header modulus

`find stdlib -maxdepth 1 -name '*.hp' | sort` returns **23** headers. The canonical
modulus used by `docs/stdlib/REGRESSION_INVENTORY.md` is **21**; it omits
`stdlib/allocator.hp` and `stdlib/memory.hp` (they were added to the tree after that
snapshot). This document inventories **all 23** because both omitted headers are
allocation/buffer helpers and therefore inside the requested categories.

### 0.2 What was inventoried

A public free function or class method is in scope when it accepts or produces any of:
user input (stdin, SDL events), file/directory paths, network bytes, JSON text/values,
strings or buffers, thread synchronization primitives, or memory allocation/release.
Pure scalar observers (`size()`, `width()`, `valid()`, `level()`, `duration_ms()`,
clock getters, `log_level_name`, etc.) are **out of scope** and listed only in the
"out of scope" note for their header. `static` internal helpers are included only when
they touch external data (e.g. `http_sink_write`, `stdlib_mem_check_range`,
`stdlib_meta_record`).

Result: **230 in-scope functions** across the 23 headers (per-header counts in §2).

### 0.3 The three evidence states

Each function is labelled with one or more of:

- **DOCUMENTED** — the header source states the validation rule, failure return, and
  cleanup/lifetime behavior in comments or in explicit guard code. This is a claim
  about the *contract written in the header*, not about correctness.
- **OBSERVED** — a call-site for the function's bare name was found by
  `grep -rlw <name> tests/stdlib --include='*.cp'`. This is a **static textual call-site**,
  the same weak signal used by `docs/stdlib/REGRESSION_INVENTORY.md`; it does not prove
  the line executed, that a branch ran, or that an assertion checked the result. Where
  the only call-sites are in a conditionally-run suite, that is noted (see §0.5).
- **UNTESTED** — no call-site found in `tests/stdlib/**/*.cp`. "No direct call-site"
  is used for internal `static` helpers that are only reached indirectly.

### 0.4 Exact evidence-path aliases

To keep the inventory tables readable, test evidence uses these aliases. Each alias
expands to the exact paths below (all relative to the repository root).

| Alias | Exact evidence paths |
|---|---|
| `FS-D` | `tests/stdlib/deep_fs_task5.cp`, `tests/stdlib/deep_fs_time.cp` |
| `FS-S` | `tests/stdlib/system_safety/fs_basic_safety.cp`, `fs_ops_safety.cp`, `fs_longpath_safety.cp`, `fs_symlink_safety.cp`, `fs_probe_tmp.cp` |
| `IO-D` | `tests/stdlib/deep_io_1byte.cp`, `deep_io_task2.cp`, `deep_io_token.cp` |
| `FILE-D` | `tests/stdlib/deep_file_lines.cp`, `deep_file_lifecycle.cp`, `deep_file_task2.cp`, `deep_cross_family.cp` |
| `IO-S` | `tests/stdlib/system_safety/io_file_lifecycle.cp`, `io_file_lines.cp`, `io_file_partial_modes.cp`, `io_file_fd_exhaustion.cp`, `io_file_empty_large.cp` |
| `NET-D` | `tests/stdlib/deep_net.cp`, `deep_net_task5.cp` |
| `NET-S` | `tests/stdlib/system_safety/net_invalid.cp`, `net_partial.cp`, `net_signal.cp`, `net_cycles.cp`, `net_refusal.cp` |
| `HTTP` | `tests/stdlib/http_test.cp`, `tests/stdlib/examples/http_demo.cp` |
| `JSON-D` | `tests/stdlib/deep_json_stress.cp`, `deep_json_types.cp`, `deep_json_task5.cp`, `deep_cross_family.cp` |
| `JSON-S` | `tests/stdlib/system_safety/json_parse_edges.cp`, `json_lifecycle.cp`, `json_numeric_edges.cp` |
| `STR-D` | `tests/stdlib/deep_string_task4.cp`, `deep_string_append.cp`, `deep_string_view.cp` |
| `STR-F` | `tests/stdlib/fuzz/f100_strview_utf8.cp`, `f100_strview_accessors.cp`, `f100_strview_append.cp`, `f100_strview_reserve_copy.cp`, `fuzz_string.cp` |
| `MEM` | `tests/stdlib/deep_memory_bounds.cp`, `tests/stdlib/allocator/diag_identity.cp`, `diag_liveness.cp`, `diag_telemetry.cp`, `fuzz/f100_mem_guarded.cp`, `f100_mem_tokens.cp` |
| `ALLOC` | `tests/stdlib/allocator/allocator_test.cp`, `allocator/overflow_count_size.cp` |
| `FAULT` | `tests/stdlib/allocation_failure/af_*.cp`, `tests/stdlib/regressions/adv_alloc_failure.cp`, `adv_integer_bounds.cp` |
| `THREAD-D` | `tests/stdlib/deep_thread_guard.cp`, `deep_thread_nested.cp`, `deep_thread_task5.cp` |
| `THREAD-S` | `tests/stdlib/system_safety/thread_condvar.cp`, `thread_contention.cp`, `thread_lifecycle.cp`, `thread_deadlock.cp`, `fuzz/f100_thread_condvar.cp` |
| `LOG` | `tests/stdlib/deep_log_task5.cp`, `tests/stdlib/log_test.cp`, `fuzz/f100_log_lines.cp` |
| `TIME` | `tests/stdlib/deep_fs_time.cp`, `tests/stdlib/fuzz/f100_time_iso8601.cp`, `tests/stdlib/agent2/extra_time_boundaries.cp` |
| `SDL` | `tests/stdlib/sdl_test.cp` (conditional: skipped when SDL3 dev files absent) |
| `GUI` | `tests/stdlib/gui_test.cp` (conditional: skipped when SDL3 absent) |
| `IMAGE` | `tests/stdlib/image_test.cp` |
| `VIDEO` | `tests/stdlib/video_test.cp` (conditional: skipped when ffmpeg CLI absent) |
| `AUDIO` | `tests/stdlib/audio_test.cp` (uses null backend) |
| `ASYNC` | `tests/stdlib/async_test.cp` (conditional: skipped when libuv absent) |
| `ADV` | `tests/stdlib/adversarial.cp`, `tests/stdlib/regressions/adv_*.cp`, `tests/stdlib/system_safety/error_hub_*.cp` |
| `COL` | `tests/stdlib/deep_collections_*.cp`, `tests/stdlib/collections_test.cp`, `tests/stdlib/container_safety/cs_*.cp` |
| `VEC` | `tests/stdlib/deep_vector_task4.cp`, `tests/stdlib/container_safety/cs_*.cp` |

### 0.5 Runner caveat

`tests/stdlib/run_stdlib_tests.sh` builds and runs `sdl_test`, `gui_test`, `http_test`,
`async_test`, and `video_test` **only if** the corresponding dev dependency is present
(lines 156–166). An OBSERVED label on a function whose only evidence is `SDL`, `GUI`,
`HTTP`, `ASYNC`, or `VIDEO` means "call-site exists in a suite that may be SKIPPED",
not "verified on this host". The deep corpus runner is
`tests/stdlib/run_deep_tests.sh` (see `docs/stdlib/TASK6_FINAL_REPORT.md §1–2`);
the static call-site method is documented in `docs/stdlib/REGRESSION_INVENTORY.md`.

### 0.6 Environment variables and process arguments — none

`grep -rnE 'getenv|setenv|unsetenv|environ|argv|argc|getopt|system\(|popen|exec[lv]|fork\('
stdlib/` returns **no matches** across all 23 headers. No public stdlib function
processes environment variables or process arguments; there is no attack surface in
those two categories to inventory. (Program-wide init is delegated to libcurl/SDL/libuv
inside the wrapper constructors, not to env/argv parsing in these headers.)

## 1. Headline counts

| Metric | Count | Derivation |
|---|---:|---|
| `.hp` headers on disk under `stdlib/` | 23 | `find stdlib -maxdepth 1 -name '*.hp'` |
| Canonical modulus in `REGRESSION_INVENTORY.md` | 21 | `docs/stdlib/REGRESSION_INVENTORY.md §2` |
| Headers represented in the master inventory | 23 | all on-disk headers, incl. `allocator.hp`, `memory.hp` |
| In-scope functions explicitly inventoried (master-table rows) | 230 | counted in §2 (`grep -cE '^\| [0-9]+ \| .stdlib/'`) |
| Master-table rows with an OBSERVED call-site | 228 | §3 evidence column; 230 − 2 UNTESTED rows |
| Master-table rows with no test call-site | 2 | rows #48 and #143 (§5) |
| Functions with no direct call-site anywhere in `tests/stdlib/**/*.cp` | 4 | §5 (2 table rows + 2 outside the table) |
| Requested categories with ≥1 in-scope function | 7 | paths/file I/O, network, JSON, strings/buffers, thread sync, allocation, SDL input/media (§3.1–3.7) |
| Requested categories with zero functions | 2 | environment variables, process arguments (§0.6) |
| Master-table rows with a documented contract | 230 | §3 (every row states validation/failure/cleanup from the header source) |

## 2. Per-header master-table row counts

The table in §3 lists **230** named functions. Its distribution by header (verified with
`grep -oE '^\| [0-9]+ \| `stdlib/[a-z]+\.hp' docs/security/stdlib-attack-surface.md | sort | uniq -c`):

| Header | Rows | Trivial observers excluded from rows (out of scope per §0.2) |
|---|---:|---|
| `stdlib/fs.hp` | 8 | — |
| `stdlib/io.hp` | 6 | `println` (no external data; example-only call-site) |
| `stdlib/file.hp` | 11 | `is_open`, `native` |
| `stdlib/log.hp` | 8 | `log_level_name`, `log_level_parse`'s helpers, `valid`, `native`, `level`, `enabled`, `set_level`, `log_stderr`, `log_stdout` |
| `stdlib/net.hp` | 10 | `is_valid` (x2), `descriptor` (x2) |
| `stdlib/http.hp` | 13 | `http_version`, `valid` (x4), `native`, `header_lines` |
| `stdlib/async.hp` | 8 | `valid` (x2), `native`, `now_ms`, `is_active`, `stop`, `sleep_ms`, `uv_error_text`, `stdlib_timer_release_handle` |
| `stdlib/json.hp` | 20 | `valid` (x3), type predicates, `size`, `root` |
| `stdlib/string.hp` | 18 | `size`, `capacity`, `valid`, `clear`, `c_str`, `size_view`, `data` |
| `stdlib/memory.hp` | 12 | — |
| `stdlib/collections.hp` | 11 | scalar-key and checked out-pointer methods (named in §3.4 note) |
| `stdlib/error.hp` | 18 | `category_name`, telemetry toggles/reset, `stdlib_meta_find`, `stdlib_safe_div`, `stdlib_alloc_length`, `stdlib_alloc_elem_size`, `error` accessors |
| `stdlib/allocator.hp` | 19 | `armed_now` |
| `stdlib/thread.hp` | 15 | `is_valid` (x3), `native`, `holds`, `is_joinable` |
| `stdlib/time.hp` | 1 | `time_now_ns/ms`, `time_monotonic_ns/ms`, `time_sleep_ms` |
| `stdlib/input.hp` | 10 | — |
| `stdlib/window.hp` | 6 | `valid`, `native`, `subsystem_flags` |
| `stdlib/gui.hp` | 10 | `valid`, `native`, `input_begin/end`, `window_end`, layout/render/style helpers |
| `stdlib/image.hp` | 7 | `valid`, `width`, `height`, `channels`, `size_bytes` |
| `stdlib/graphics.hp` | 6 | `valid`, `width`, `height`, `native`, draw/render helpers |
| `stdlib/video.hp` | 9 | `valid` (x3), metadata accessors, `video_codec_id`, `video_stream_params` |
| `stdlib/audio.hp` | 4 | `valid` (x5), `native` (x2), metadata/state helpers, `audio_sleep_ms` (named in §3.7 note) |
| **Total** | **230** | — |

## 3. DOCUMENTED register — the full inventory

Every row below is **DOCUMENTED**: the "validation / failure / cleanup" cells are
taken verbatim from guard code or comments in the named `stdlib/<header>:<line>`.
The evidence column supplies the OBSERVED / UNTESTED classification (§4 / §5).

Trust-boundary legend:
- **PATH** — caller-supplied filesystem path/name string.
- **STDIN** — bytes read from standard input.
- **NET** — bytes/URLs/headers from a socket or libcurl.
- **JSON** — parsed/unparsed JSON text or values, or builder inputs.
- **BUF** — caller-supplied pointer + length/capacity, or returned owned buffer.
- **STR** — caller-supplied NUL-terminated string.
- **SYNC** — mutex/condvar/thread state manipulated by multiple threads.
- **ALLOC** — allocation request, metadata key, generation token, or ownership handle.
- **EVENT** — SDL event / keyboard / mouse state from the windowing system.
- **MEDIA** — image/audio/video frames, pixel buffers, or decoder paths.

### 3.1 File paths and file I/O

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 1 | `stdlib/fs.hp:17` | `int fs_exists(const char *path)` | PATH; `path` | `path == NULL` → 0; `stat()` result | returns 0 (no error record) | none (no resource) | FS-D, FS-S |
| 2 | `stdlib/fs.hp:24` | `int fs_is_file(const char *path)` | PATH | NULL/`stat` fail → 0; `S_ISREG` | returns 0 | none | FS-D, FS-S |
| 3 | `stdlib/fs.hp:31` | `int fs_is_dir(const char *path)` | PATH | NULL/`stat` fail → 0; `S_ISDIR` | returns 0 | none | FS-D, FS-S |
| 4 | `stdlib/fs.hp:39` | `long long fs_size(const char *path, error *err)` | PATH | NULL → INVALID; `stat` fail → IO/`strerror(errno)` | `-1`, `err` set | `error_reset` on entry | FS-D, FS-S |
| 5 | `stdlib/fs.hp:54` | `int fs_remove(const char *path, error *err)` | PATH | NULL → INVALID; `unlink` fail → IO | `STDLIB_FAIL`, `err` set | none | FS-D, FS-S |
| 6 | `stdlib/fs.hp:68` | `int fs_rename(const char *from, const char *to, error *err)` | PATH (two) | either NULL → INVALID; `rename` fail → IO | `STDLIB_FAIL`, `err` set | none | FS-D, FS-S |
| 7 | `stdlib/fs.hp:82` | `int fs_mkdir(const char *path, error *err)` | PATH | NULL → INVALID; `mkdir(path,0777)` fail → IO | `STDLIB_FAIL`, `err` set | none | FS-D, FS-S |
| 8 | `stdlib/fs.hp:98` | `int fs_mkdtemp(char *tmpl, size_t size, error *err)` | PATH (mutable template); size | `tmpl==NULL || size<7`, `strnlen>=size`, length ≥6, suffix `XXXXXX`, `mkdtemp` | `STDLIB_FAIL`, `err` set | **known gap**: consumes `XXXXXX` template on failure (documented in `TASK6_FINAL_REPORT.md §8`) | FS-D, FS-S |
| 9 | `stdlib/io.hp:30` | `int stdlib_io_token(char *buf, size_t size)` | STDIN + BUF | NULL/0 → 1; `stored+1<size`, overflow flag, drains to delimiter | 1=EOF, 2=truncated, 0=ok; always NUL-terminates | stream drained on truncation | IO-D, ADV |
| 10 | `stdlib/io.hp:57` | `int read_line(char *buf, size_t size, error *err)` | STDIN + BUF | NULL/0, `size>INT_MAX`, size==1 path, fgets, exact-fit peek, CRLF consume, drain | STDLIB_OK/EOF/FAIL(BOUNDS/IO) | overlong line drains, buffer NUL-terminated/cleared | IO-D, FILE-D, IO-S, ADV |
| 11 | `stdlib/io.hp:149` | `int read_int(int *out, error *err)` | STDIN (numeric token) | NULL out; token length; `strtol` end/`errno` ERANGE; int min/max | STDLIB_FAIL with INVALID/RANGE/IO | 64-byte stack token, no heap | IO-D, ADV |
| 12 | `stdlib/io.hp:185` | `int read_long(long *out, error *err)` | STDIN | as above via `strtoll` + LONG_MIN/MAX | STDLIB_FAIL, `err` set | stack token | IO-D, ADV |
| 13 | `stdlib/io.hp:221` | `int read_float(float *out, error *err)` | STDIN | as above via `strtof`; rejects `!isfinite` and ERANGE | STDLIB_FAIL RANGE | stack token | IO-D |
| 14 | `stdlib/io.hp:259` | `int read_double(double *out, error *err)` | STDIN | as above via `strtod`; rejects non-finite | STDLIB_FAIL RANGE | stack token | IO-D, ADV |
| 15 | `stdlib/file.hp:20` | `file::file(const char *path, const char *mode)` | PATH + STR (mode) | NULL path/mode → NULL stream; `fopen` | invalid handle; `is_open()==0` | destructor closes only if owned | FILE-D, IO-S |
| 16 | `stdlib/file.hp:33` | `int file::read(char *buf, size_t size, error *err)` | BUF | NULL/0 → FAIL; null handle; `fread`; `ferror` | OK/EOF/FAIL | RAII close in drop | FILE-D, IO-S |
| 17 | `stdlib/file.hp:55` | `int file::read_line(char *buf, size_t size, error *err)` | BUF from file | as #10 plus size>INT_MAX, byte-at-a-time for size==1 | OK/EOF/FAIL(BOUNDS/IO) | drains overlong line; clears buffer | FILE-D, IO-S, ADV |
| 18 | `stdlib/file.hp:156` | `int file::write(const char *text)` | STR | NULL text/null handle → 0; `fputs != EOF` | returns 0 | stream stays owned by handle | FILE-D, IO-S |
| 19 | `stdlib/file.hp:162` | `int file::write_bytes(const char *data, size_t size, error *err)` | BUF | size>0 && NULL → FAIL; null handle; `fwrite != size` (short write) | STDLIB_FAIL, `err` set | none beyond RAII close | FILE-D, IO-S |
| 20 | `stdlib/file.hp:179` | `int file::seek(long offset, int whence, error *err)` | caller offset/whence | null handle; `fseek != 0` | STDLIB_FAIL, `err` set | none | FILE-D, IO-S |
| 21 | `stdlib/file.hp:192` | `int file::flush(error *err)` | stream state | null handle; `fflush != 0` | STDLIB_FAIL, `err` set | none | FILE-D, IO-S |
| 22 | `stdlib/file.hp:206` | `int file::close(error *err)` | owned/borrowed handle | null handle → LIFECYCLE; only `fclose` if `owns` | STDLIB_FAIL if already closed / fclose fail | sets stream=NULL, owns=0 (double-close safe) | FILE-D, IO-S |
| 23 | `stdlib/file.hp:225` | `void file::drop(void)` | owned handle | closes only if `owns` | n/a (void) | idempotent; stream=NULL, owns=0 | FILE-D, IO-S |
| 24 | `stdlib/file.hp:234` | `file file_borrow(FILE *handle)` | borrowed FILE* | sets `owns=0`; no NULL guard | returns inert wrapper for NULL | drop never closes borrowed handle | FILE-D, IO-S |
| 25 | `stdlib/file.hp:242` | `file file_temp(error *err)` | `tmpfile()` | NULL result → FAIL, `owns=0` | invalid handle, `err` set | `tmpfile` unlinks on close; drop frees | FILE-D, IO-S |
| 26 | `stdlib/log.hp:47` | `int log_level_parse(const char *text, error *err)` | STR | NULL → INVALID; case-insensitive match over 6 levels | `-1`, `err` set | none | LOG |
| 27 | `stdlib/log.hp:69` | `log_sink::log_sink(FILE *stream, int owns, int threshold)` | borrowed/owned FILE* | `owns` forced 0 if stream NULL | invalid sink | drop closes only if owns | LOG |
| 28 | `stdlib/log.hp:98` | `int log_sink::write(int severity, const char *message, const char *const *keys, const long long *values, size_t field_count, const char *file, int line, error *err)` | STR + BUF arrays (message, keys, values, file) | sink validity; field arrays non-NULL when count>0; per-field snprintf truncation checks; newline/NUL bounds; threshold | FAIL BOUNDS/IO, `err` set | `flockfile`/`funlockfile` on every path incl. short write | LOG |
| 29 | `stdlib/log.hp:179` | `int log_sink::info(const char *message, error *err)` | STR | delegates to #28 | as #28 | as #28 | LOG |
| 30 | `stdlib/log.hp:183` | `int log_sink::warn(const char *message, error *err)` | STR | delegates to #28 | as #28 | as #28 | LOG |
| 31 | `stdlib/log.hp:187` | `int log_sink::failure(const char *message, error *err)` | STR | delegates to #28 | as #28 | as #28 | LOG |
| 32 | `stdlib/log.hp:192` | `void log_sink::drop(void)` | owned FILE* | closes only if owns | n/a | stream=NULL, owns=0 | LOG |
| 33 | `stdlib/log.hp:201` | `log_sink log_open_file(const char *path, int threshold, error *err)` | PATH | NULL → INVALID; `fopen(path,"a")` fail → IO | invalid sink, `err` set | drop closes owned file | LOG |

### 3.2 Network (sockets, HTTP, async file I/O)

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 34 | `stdlib/net.hp:24` | `connection::connection(int descriptor)` | borrowed/owned fd | `owns = descriptor>=0` | inert for negative fd | drop closes only if owns | NET-D, NET-S |
| 35 | `stdlib/net.hp:41` | `int connection::read(char *buf, size_t capacity, error *err)` | NET + BUF | `is_valid`, NULL/0, `capacity>INT_MAX` clamp, EINTR retry, `ferror`-style n<0, n==0 | OK / EOF / FAIL(IO/INVALID) | fd untouched; RAII close at drop | NET-D, NET-S |
| 36 | `stdlib/net.hp:64` | `int connection::write(const char *data, size_t length, error *err)` | NET + BUF | is_valid, NULL, EINTR retry, n==0 no-progress guard | STDLIB_FAIL IO/INVALID | partial writes already sent; no rollback | NET-D, NET-S |
| 37 | `stdlib/net.hp:93` | `void connection::drop(void)` | owned fd | closes only if owns && fd≥0 | n/a | fd=-1, owns=0 (idempotent) | NET-D, NET-S |
| 38 | `stdlib/net.hp:106` | `listener::listener(int port)` | caller port; binds loopback | port 0..65535; `socket`, `setsockopt`, `bind`, `listen`; closes fd on bind/listen fail | fd=-1, owns=0 | close on partial init | NET-D, NET-S |
| 39 | `stdlib/net.hp:140` | `int listener::bound_port(void)` | bound socket state | is_valid; `getsockname` fail | returns 0 | none | NET-D |
| 40 | `stdlib/net.hp:150` | `connection listener::accept(void)` | incoming connection | is_valid; EINTR retry | returns invalid connection (fd=-1) | caller must drop; fd owned | NET-D, NET-S |
| 41 | `stdlib/net.hp:161` | `void listener::drop(void)` | owned fd | closes only if owns | n/a | fd=-1, owns=0 | NET-D, NET-S |
| 42 | `stdlib/net.hp:170` | `listener listener_borrow(int fd)` | borrowed fd | sets owns=0 | inert wrapper | drop never closes | NET-D |
| 43 | `stdlib/net.hp:177` | `connection connection_borrow(int fd)` | borrowed fd | sets owns=0 | inert wrapper | drop never closes | NET-D, NET-S |
| 44 | `stdlib/http.hp:20` | `http::http(error *err)` | process-wide libcurl init | `curl_global_init != CURLE_OK` → RESOURCE | `initialized=0`, err set | drop calls `curl_global_cleanup` | HTTP |
| 45 | `stdlib/http.hp:36` | `void http::drop(void)` | global init state | only cleans if initialized | n/a | initialized=0 | HTTP |
| 46 | `stdlib/http.hp:65` | `int http_headers::add(const char *line, error *err)` | STR (header line) | NULL → UNSUPPORTED; `curl_slist_append` fail → RESOURCE | STDLIB_FAIL, err set | prior list retained | HTTP |
| 47 | `stdlib/http.hp:81` | `void http_headers::drop(void)` | owned slist | frees when non-NULL | n/a | `curl_slist_free_all`, list=NULL | HTTP |
| 48 | `stdlib/http.hp:97` | `size_t http_sink_write(char *ptr, size_t size, size_t nmemb, void *userdata)` | NET bytes (libcurl callback) | NULL sink/buf/0; `nmemb>SIZE_MAX/size`; `bytes==0`; overflow-safe `used` bound, truncates and sets `overflow` | returns 0 to abort on overflow | NUL-terminates within caller buffer | **UNTESTED (no direct call-site)** — invoked only via libcurl from #53–55 |
| 49 | `stdlib/http.hp:136` | `http_client::http_client(error *err)` | curl easy handle | `curl_easy_init == NULL` → RESOURCE | invalid handle | drop cleans handle + header_copy | HTTP |
| 50 | `stdlib/http.hp:148` | `int http_client::set_timeout(long seconds, error *err)` | caller timeout | handle NULL / seconds<0 → INVALID; setopt fail | STDLIB_FAIL | none | HTTP |
| 51 | `stdlib/http.hp:163` | `int http_client::follow_redirects(int enabled, error *err)` | caller flag | handle NULL; setopt fail | STDLIB_FAIL | none | HTTP |
| 52 | `stdlib/http.hp:177` | `int http_client::set_headers(http_headers *headers, error *err)` | borrowed slist (STR lines) | handle/headers NULL; per-line `curl_slist_append` fail → frees partial copy; setopt fail | STDLIB_FAIL | old `header_copy` freed; partial copy freed on failure | HTTP |
| 53 | `stdlib/http.hp:217` | `int http_client::request(const char *url, const char *body, char *buf, size_t size, error *err)` | NET URL + BODY + BUF | handle/url/buf/0; `buf[0]='\0'`; setopt chain; `curl_easy_perform`; `sink.overflow`; WRITEDATA reset on all exits | FAIL IO/INVALID/BOUNDS | WRITEDATA cleared, err set; response truncated to size-1 | HTTP, ADV |
| 54 | `stdlib/http.hp:261` | `int http_client::get(const char *url, char *buf, size_t size, error *err)` | NET URL + BUF | delegates to #53 (body=NULL) | as #53 | as #53 | HTTP |
| 55 | `stdlib/http.hp:265` | `int http_client::post(const char *url, const char *body, char *buf, size_t size, error *err)` | NET URL + BODY + BUF | delegates to #53 | as #53 | as #53 | HTTP |
| 56 | `stdlib/http.hp:286` | `void http_client::drop(void)` | owned handle + header_copy | frees both when non-NULL | n/a | handle=NULL, header_copy=NULL | HTTP |
| 57 | `stdlib/async.hp:21` | `loop::loop(error *err)` | uv loop init | `uv_loop_init != 0` → RESOURCE | `initialized=0`, err set | drop drains + closes | ASYNC |
| 58 | `stdlib/async.hp:42` | `int loop::run(int mode, error *err)` | caller `mode` cast to `uv_run_mode` | valid loop; `uv_run` result logged (not fatal) | STDLIB_OK even if handles remain | loop stays valid | ASYNC |
| 59 | `stdlib/async.hp:67` | `void loop::drop(void)` | libuv handles | bounded drain `guard<1000`; `uv_loop_close` fail logged | n/a | initialized=0; warns if still busy | ASYNC |
| 60 | `stdlib/async.hp:92` | `timer::timer(uv_loop_t *owner_loop, error *err)` | borrowed native loop; heap handle | NULL loop → INVALID; alloc fail → MEMORY; `uv_timer_init` fail releases handle | `initialized=0`, err set | handle released on init failure; drop uses uv_close callback | ASYNC |
| 61 | `stdlib/async.hp:122` | `int timer::start(uv_timer_cb callback, unsigned long long timeout_ms, unsigned long long repeat_ms, void *user, error *err)` | callback + opaque user ptr | valid timer, callback NULL; `uv_timer_start` fail → IO | STDLIB_FAIL | handle->data set to user | ASYNC |
| 62 | `stdlib/async.hp:137` | `int timer::stop(error *err)` | timer state | valid timer; `uv_timer_stop` fail → IO | STDLIB_FAIL | none | ASYNC |
| 63 | `stdlib/async.hp:159` | `void timer::drop(void)` | heap handle + loop | `uv_timer_stop`, clears data, `uv_close` with release callback, bounded drain | n/a | handle freed by `stdlib_timer_release_handle`, not by drop directly | ASYNC |
| 64 | `stdlib/async.hp:181` | `long long read_file(loop *owner, const char *path, char *buf, size_t size, error *err)` | PATH + BUF | owner/path/buf/0; `size>UINT_MAX`; `uv_fs_open` <0; `uv_fs_read` <0 | `-1`, err set | `uv_fs_req_cleanup` after every op; fd closed | ASYNC |

### 3.3 JSON

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 65 | `stdlib/json.hp:24` | `json_view::json_view(const yyjson_val *value)` | borrowed yyjson value | stores pointer; no dereference | invalid view if NULL | never frees | JSON-D |
| 66 | `stdlib/json.hp:52` | `int json_view::get_int(int *out, error *err)` | JSON number | NULL out / non-int → INVALID; sint/uint range → RANGE | STDLIB_FAIL, err set | none | JSON-D, JSON-S |
| 67 | `stdlib/json.hp:79` | `int json_view::get_double(double *out, error *err)` | JSON number | NULL/non-number → INVALID | STDLIB_FAIL | none | JSON-D, JSON-S |
| 68 | `stdlib/json.hp:90` | `int json_view::get_bool(int *out, error *err)` | JSON bool | NULL/non-bool → INVALID | STDLIB_FAIL | none | JSON-D, JSON-S |
| 69 | `stdlib/json.hp:102` | `int json_view::get_string(const char **out, error *err)` | borrowed JSON string | NULL/non-string → INVALID | STDLIB_FAIL | view borrowed; owner must live | JSON-D, JSON-S |
| 70 | `stdlib/json.hp:121` | `json_view json_view::at(size_t index)` | JSON array index | `yyjson_arr_get` (no explicit bounds guard; yyjson returns NULL) | invalid view for OOB | none | JSON-D |
| 71 | `stdlib/json.hp:126` | `json_view json_view::member(const char *key)` | STR key | NULL value/key → NULL view | invalid view | none | JSON-D, JSON-S |
| 72 | `stdlib/json.hp:133` | `char *json_view::write(size_t *length)` | owned buffer out | NULL value → NULL | returns NULL | caller must `json_free` | JSON-D |
| 73 | `stdlib/json.hp:145` | `json::json(const char *text, error *err)` | JSON text (STR) | NULL → invalid doc, no error; `yyjson_read_opts(...strlen(text))` fail → message from `yyjson_read_err` | `valid()==0`, err set | drop frees doc | JSON-D, JSON-S |
| 74 | `stdlib/json.hp:172` | `void json::drop(void)` | owned doc | frees when non-NULL | n/a | doc=NULL, idempotent | JSON-D, JSON-S |
| 75 | `stdlib/json.hp:181` | `json json_parse_file(const char *path, error *err)` | PATH | NULL → INVALID IO; `yyjson_read_file` fail → IO + `info.msg` | invalid json, err set | returned json owns doc; drop frees | JSON-D, ADV |
| 76 | `stdlib/json.hp:197` | `json_view json_view_of(const yyjson_val *value)` | borrowed yyjson value | wraps pointer | invalid view if NULL | never frees | JSON-D |
| 77 | `stdlib/json.hp:202` | `void json_free(char *text)` | owned serialized buffer | calls `free` (NULL-safe) | n/a | frees exactly once | JSON-D |
| 78 | `stdlib/json.hp:210` | `json_builder::json_builder(void)` | mut doc allocation | `yyjson_mut_doc_new`/obj fail → invalid | `valid()==0` | drop frees doc | JSON-D |
| 79 | `stdlib/json.hp:224` | `int json_builder::add_int(const char *key, int value, error *err)` | STR key | valid + key NULL → INVALID; add fail → MEMORY | STDLIB_FAIL | doc retained | JSON-D |
| 80 | `stdlib/json.hp:239` | `int json_builder::add_double(const char *key, double value, error *err)` | STR key + value | as #79 plus `!isfinite` → RANGE | STDLIB_FAIL | doc retained | JSON-D |
| 81 | `stdlib/json.hp:259` | `int json_builder::add_bool(const char *key, int value, error *err)` | STR key | as #79 | STDLIB_FAIL | doc retained | JSON-D |
| 82 | `stdlib/json.hp:275` | `int json_builder::add_string(const char *key, const char *value, error *err)` | STR key + STR value | valid + NULL key/value → INVALID; add fail → MEMORY; copies value | STDLIB_FAIL | caller keeps buffer | JSON-D |
| 83 | `stdlib/json.hp:290` | `char *json_builder::write(size_t *length)` | owned buffer out | invalid builder → NULL | returns NULL | caller must `json_free` | JSON-D |
| 84 | `stdlib/json.hp:296` | `void json_builder::drop(void)` | owned mut doc | frees when non-NULL | n/a | doc=NULL, root=NULL | JSON-D, JSON-S |

### 3.4 Strings, views, and buffers

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 85 | `stdlib/string.hp:30` | `int stdlib_utf8_validate(const char *data, size_t len)` | BUF + len | len==0 → 1; NULL with len>0 → 0; RFC 3629 overlong/surrogate/>U+10FFFF/truncation guards | returns 0 | read-only | STR-D, STR-F |
| 86 | `stdlib/string.hp:84` | `string_view::string_view(const char *chars, size_t count)` | borrowed BUF | stores pointer+len; no deref | invalid if NULL+count>0 | never frees | STR-D |
| 87 | `stdlib/string.hp:97` | `int string_view::equals(const char *other)` | STR | NULL → 0; NULL chars with count>0 → 0; strlen/memcmp | returns 0 | none | STR-D, STR-F |
| 88 | `stdlib/string.hp:111` | `int string_view::is_valid_utf8(void)` | borrowed BUF | delegates to #85 | 0/1 | none | STR-D, STR-F |
| 89 | `stdlib/string.hp:121` | `string::string(const char *text)` | STR | NULL→empty; `n<STDLIB_ALLOC_LIMIT`; alloc fail → invalid; copies n bytes + NUL | `valid()==0` | drop frees buffer | STR-D, FAULT |
| 90 | `stdlib/string.hp:151` | `char string::at(size_t index)` | caller index | NULL buffer / `index>=count` → '\0' | returns '\0' | none | STR-D |
| 91 | `stdlib/string.hp:162` | `int string::equals(const char *other)` | STR | NULL → note INVALID, 0; length + memcmp | 0/1 | none | STR-D, ADV |
| 92 | `stdlib/string.hp:181` | `int string::compare(const char *other)` | STR | NULL → note INVALID, returns 1; strcmp | -1/0/1 | none | STR-D, ADV |
| 93 | `stdlib/string.hp:201` | `long string::find(const char *needle)` | STR | NULL chars/needle → -1; strstr | -1 or offset | none | STR-D |
| 94 | `stdlib/string.hp:212` | `int string::reserve(size_t want)` | caller capacity | want<=space no-op; `>STDLIB_ALLOC_LIMIT` → -1; alloc fail → -1 | -1, unchanged (failure-atomic) | old buffer released only after new succeeds | STR-D, FAULT |
| 95 | `stdlib/string.hp:240` | `int string::append(const char *text)` | STR (incl. self/alias) | NULL → -1; overflow guards; interior-pointer alias window; reserve then `memmove`/`memcpy` | -1, unchanged | alias-safe; old buffer freed in reserve | STR-D, FAULT, ADV |
| 96 | `stdlib/string.hp:306` | `int string::copy_from(string *other)` | BUF (owned source) | NULL → -1; self no-op; `n>=LIMIT`; alloc fail → -1 | -1, unchanged | releases old buffer after new allocated | STR-D, FAULT |
| 97 | `stdlib/string.hp:334` | `int string::is_valid_utf8(void)` | owned BUF | delegates to #85 | 0/1 | none | STR-D, STR-F |
| 98 | `stdlib/string.hp:339` | `int string::substr(size_t start, size_t n, char *out, size_t out_size)` | BUF out | NULL/0, `start>count`, clamps n to available and out_size-1, memmove + NUL | -1 | always NUL-terminates | STR-D, ADV |
| 99 | `stdlib/string.hp:358` | `size_t string::count_fields(char sep)` | caller separator | none needed (read-only) | returns count | none | STR-D |
| 100 | `stdlib/string.hp:368` | `int string::field(char sep, size_t index, char *out, size_t out_size)` | BUF out | NULL/0, clamps to out_size-1, memmove + NUL | -1 | always NUL-terminates | STR-D, ADV |
| 101 | `stdlib/string.hp:393` | `string_view string::to_view(void)` | borrowed view of own BUF | wraps chars+count | invalid after drop/growth | caller responsible for lifetime | STR-D, ADV |
| 102 | `stdlib/string.hp:400` | `void string::drop(void)` | owned buffer | releases when non-NULL | n/a | chars=NULL, count=0, space=0 | STR-D, FAULT |
| 103 | `stdlib/memory.hp:73` | `size_t stdlib_mem_capacity(const void *ptr)` | ALLOC metadata key | returns `stdlib_alloc_size` | 0 for untracked | none | MEM |
| 104 | `stdlib/memory.hp:81` | `int stdlib_mem_count(const void *ptr, size_t elem_size, size_t *out_count)` | ALLOC key + out | `stdlib_alloc_query_length`; out zeroed on failure | -1 | none | MEM |
| 105 | `stdlib/memory.hp:91` | `unsigned long long stdlib_mem_token(const void *ptr)` | ALLOC generation token | `stdlib_alloc_generation` | 0 for untracked | none | MEM |
| 106 | `stdlib/memory.hp:98` | `int stdlib_mem_check_token(const void *ptr, unsigned long long token, error *err)` | ALLOC token | token==0 opts out; generation mismatch → LIFECYCLE | STDLIB_FAIL | none | MEM |
| 107 | `stdlib/memory.hp:111` | `int stdlib_mem_check(const void *ptr, unsigned long long token, error *err)` | ALLOC key + token | NULL/untracked → TYPE_CONFUSION; then token | STDLIB_FAIL | none | MEM |
| 108 | `stdlib/memory.hp:126` | `int stdlib_mem_check_range(const void *ptr, size_t off, size_t n, int is_dest, error *err)` | ALLOC key + offsets | NULL, cap==0, `off>cap`, `n>cap-off` (subtraction, overflow-safe) | STDLIB_FAIL with NULL/BOUNDS/TYPE | none | MEM |
| 109 | `stdlib/memory.hp:164` | `int stdlib_mem_copy(void *dst, size_t dst_off, const void *src, size_t src_off, size_t n, unsigned long long dst_token, error *err)` | BUF src/dst | both ranges checked (#108) then dst token; n==0 no-op after validation | STDLIB_FAIL; no write on failure | none (no ownership change) | MEM |
| 110 | `stdlib/memory.hp:180` | `int stdlib_mem_move(void *dst, size_t dst_off, const void *src, size_t src_off, size_t n, unsigned long long dst_token, error *err)` | BUF src/dst | as #109 but `memmove` for overlap | STDLIB_FAIL; no write on failure | none | MEM |
| 111 | `stdlib/memory.hp:196` | `int stdlib_mem_set(void *dst, size_t dst_off, int value, size_t n, unsigned long long dst_token, error *err)` | BUF dst | dst range + token; n==0 no-op | STDLIB_FAIL; no write on failure | none | MEM |
| 112 | `stdlib/memory.hp:215` | `int stdlib_mem_borrow(const void *ptr, size_t off, size_t n, unsigned long long token, error *err)` | BUF borrow | range + token; non-owning | STDLIB_FAIL | no generation change, caller keeps ownership | MEM |
| 113 | `stdlib/memory.hp:230` | `int stdlib_mem_transfer(void **dst, void **src, error *err)` | ALLOC handles | NULL/equal, *dst!=NULL, *src==NULL, source live | STDLIB_FAIL OWNERSHIP | on success bumps generation, *src=NULL | MEM |
| 114 | `stdlib/memory.hp:247` | `void stdlib_mem_release(void **ptr)` | ALLOC handle | NULL no-op | n/a | `stdlib_release`, *ptr=NULL (repeat-safe) | MEM |
| 115 | `stdlib/collections.hp:47` | `unsigned long long stdlib_map_hash_text(const char *text)` | STR | NULL-safe loop | returns FNV hash | none | COL |
| 116 | `stdlib/collections.hp:333` | `char *strmap::stdlib_copy_key(const char *text)` | STR → owned BUF | NULL → NULL; `len>=STDLIB_ALLOC_LIMIT`; alloc fail → NULL | returns NULL | caller releases copy on failure paths | COL |
| 117 | `stdlib/collections.hp:425` | `int strmap::put(const char *key, int value)` | STR key | NULL → -1; rebuild first; key copy last (failure-atomic) | -1, map unchanged | copied key released by remove/clear/drop | COL |
| 118 | `stdlib/collections.hp:470` | `int strmap::get(const char *key, int *out)` | STR + out | NULL key/out → -1 | -1 | none | COL |
| 119 | `stdlib/collections.hp:481` | `int strmap::contains(const char *key)` | STR | delegates to #118 | 0/1 | none | COL |
| 120 | `stdlib/collections.hp:486` | `int strmap::remove(const char *key)` | STR | probe; NULL key → -1 | -1 | releases owned key | COL |
| 121 | `stdlib/collections.hp:257` | `int intmap::get(int key, int *out)` | caller out BUF | NULL out → -1 | -1 | none | COL |
| 122 | `stdlib/collections.hp:779` | `int deque::at_checked(size_t index, int *out)` | caller index + out BUF | NULL out / OOB → -1 | -1 | none | COL |
| 123 | `stdlib/collections.hp:899` | `int list::at_checked(size_t index, int *out)` | caller index + out BUF | NULL out / OOB → -1 | -1 | none | COL |
| 124 | `stdlib/collections.hp:982` | `int stack::top_checked(int *out)` | out BUF | NULL out → -1 | -1 | none | COL |
| 125 | `stdlib/collections.hp:1061` | `int queue::front_checked(int *out)` | out BUF | NULL out → -1 | -1 | none | COL |

Note (collections): the remaining `collections.hp` methods accept only scalar keys or
`int *out` pointers and share the exact guards above; they are exercised by `COL` and
enumerated by name in `docs/stdlib/REGRESSION_INVENTORY.md §4` (all 63 entries reported
exercised there by the static call-site measure). They are not re-listed here to avoid
duplicating identical cells; see the coverage note after §3.7.

### 3.5 Allocation, telemetry, and error buffers

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 126 | `stdlib/error.hp:365` | `void *stdlib_alloc(size_t bytes)` | ALLOC size | bytes==0→1; `>STDLIB_ALLOC_LIMIT` → NULL; injected fail; `cplus_alloc` | NULL, note + telemetry | metadata recorded only on success | FAULT, MEM |
| 127 | `stdlib/error.hp:395` | `void *stdlib_calloc(size_t count, size_t element_size)` | ALLOC count/size | `__builtin_mul_overflow`, limit, zero-fill | NULL | metadata recorded; no leak on fail | FAULT, MEM |
| 128 | `stdlib/error.hp:422` | `void *stdlib_realloc(void *ptr, size_t bytes)` | ALLOC block + size | limit; pointer must be tracked; failure-atomic | NULL, original intact | old slot tombstoned only on success | FAULT, MEM |
| 129 | `stdlib/error.hp:477` | `size_t stdlib_alloc_size(const void *ptr)` | ALLOC key | meta lookup under spinlock | 0 untracked | none | MEM |
| 130 | `stdlib/error.hp:513` | `unsigned long long stdlib_alloc_generation(const void *ptr)` | ALLOC key | meta lookup | 0 untracked | none | MEM |
| 131 | `stdlib/error.hp:524` | `int stdlib_alloc_check_generation(const void *ptr, unsigned long long gen)` | ALLOC key + token | NULL/gen 0 → 0 | 0/1 | none | MEM |
| 132 | `stdlib/error.hp:538` | `int stdlib_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count)` | ALLOC key + out | NULL/out NULL → -1; out zeroed; safe_div | -1 | lock released on all paths | MEM |
| 133 | `stdlib/error.hp:558` | `int stdlib_transfer_ownership(void **dst, void **src)` | ALLOC handles | NULL/equal/*dst/*src/live checks, note OWNERSHIP | -1; handles unchanged | bumps generation, *src=NULL on success | MEM |
| 134 | `stdlib/error.hp:582` | `void stdlib_release(void *ptr)` | ALLOC block | NULL no-op | n/a | `stdlib_meta_forget` + `cplus_release` | FAULT, MEM |
| 135 | `stdlib/error.hp:289` | `void stdlib_meta_record(const void *p, size_t byte_size, size_t elem_size)` | ALLOC key + metadata | NULL → return; growth via `calloc`; rehash under lock | silent if rehash fails (table unchanged) | lock released; entries preserved | ADV |
| 136 | `stdlib/error.hp:194` | `void stdlib_note(int category, cplus_exit_code code, const char *message)` | STR (static message) | thread-local re-entrancy guard; callback optional | no failure (void) | reporting flag reset after callback | ADV |
| 137 | `stdlib/error.hp:671` | `void error_fail_cat(void *target, int category, cplus_exit_code code, const char *message)` | STR → error text BUF | strlen bounded to STDLIB_ERROR_SIZE-1; memcpy + NUL; NULL target allowed | sets error fields | fixed 256-byte inline buffer | ADV |
| 138 | `stdlib/error.hp:227` | `void stdlib_alloc_fail_at(long n)` | ALLOC injection counter | n>0 else 0 | n/a (void) | n/a | FAULT |
| 139 | `stdlib/error.hp:234` | `void stdlib_alloc_fail_after(long n)` | ALLOC injection counter | n>=0 else 0 | n/a | n/a | FAULT |
| 140 | `stdlib/error.hp:241` | `void stdlib_alloc_fail_reset(void)` | injection state | resets at/from/seq | n/a | n/a | FAULT |
| 141 | `stdlib/error.hp:148` | `void stdlib_telemetry_snapshot(stdlib_telemetry_stats *out)` | stats BUF | NULL no-op; atomic loads | n/a | n/a | MEM, ADV |
| 142 | `stdlib/error.hp:163` | `void stdlib_set_error_callback(stdlib_error_callback callback)` | callback pointer | atomic store-release | n/a | n/a | ADV |
| 143 | `stdlib/error.hp:127` | `void stdlib_tm_add(int index, unsigned long long amount)` | counter index | telemetry-enabled check (no index bounds guard; internal caller responsibility) | n/a | n/a | **UNTESTED (no direct call-site)** |
| 144 | `stdlib/allocator.hp:84` | `int stdlib_alloc_identity(const void *ptr, stdlib_alloc_id *out)` | ALLOC ptr + id BUF | NULL out → -1; out zeroed; tracked-only; lock | -1, out zeroed | lock released | ALLOC |
| 145 | `stdlib/allocator.hp:110` | `int stdlib_alloc_id_equal(const stdlib_alloc_id *a, const stdlib_alloc_id *b)` | ALLOC ids | NULL/zero base → 0; base+generation | 0/1 | none | ALLOC |
| 146 | `stdlib/allocator.hp:128` | `int stdlib_alloc_id_matches(const stdlib_alloc_id *id, const void *ptr)` | ALLOC id + ptr | NULL/zero/base mismatch → 0 | 0/1 | none | ALLOC |
| 147 | `stdlib/allocator.hp:145` | `size_t stdlib_alloc_view_bytes(const void *ptr)` | ALLOC key | `stdlib_alloc_size` | 0 untracked | none | ALLOC |
| 148 | `stdlib/allocator.hp:153` | `size_t stdlib_alloc_view_elem_bytes(const void *ptr)` | ALLOC key | `stdlib_alloc_elem_size` | 0/1 untracked | none | ALLOC |
| 149 | `stdlib/allocator.hp:161` | `unsigned long long stdlib_alloc_view_generation(const void *ptr)` | ALLOC key | `stdlib_alloc_generation` | 0 untracked | none | ALLOC |
| 150 | `stdlib/allocator.hp:169` | `int stdlib_alloc_view_live(const void *ptr)` | ALLOC key | generation != 0 | 0/1 | none | ALLOC |
| 151 | `stdlib/allocator.hp:178` | `size_t stdlib_alloc_view_count(const void *ptr)` | ALLOC key | `stdlib_alloc_query_length` | 0 | none | ALLOC |
| 152 | `stdlib/allocator.hp:192` | `size_t stdlib_alloc_view_remaining(const void *base, size_t offset)` | ALLOC key + offset | NULL/cap 0/offset>cap → 0; subtraction | 0 or remaining | none | ALLOC |
| 153 | `stdlib/allocator.hp:209` | `int stdlib_alloc_view_unchanged(const void *ptr, unsigned long long gen)` | ALLOC key + token | `stdlib_alloc_check_generation` | 0/1 | none | ALLOC |
| 154 | `stdlib/allocator.hp:221` | `int stdlib_alloc_view_transfer_ok(void *dst, void *src)` | ALLOC handles | NULL/equal/*dst!=NULL/*src==NULL/live | 0/1; validation only, moves nothing | none | ALLOC |
| 155 | `stdlib/allocator.hp:237` | `void stdlib_alloc_tm_snapshot(stdlib_telemetry_stats *out)` | stats BUF | delegates #141 | n/a | n/a | ALLOC |
| 156 | `stdlib/allocator.hp:245` | `void stdlib_alloc_tm_delta(const stdlib_telemetry_stats *before, const stdlib_telemetry_stats *after, stdlib_telemetry_stats *out)` | stats BUFs | NULL out no-op; NULL before/after zero out | n/a | n/a | ALLOC |
| 157 | `stdlib/allocator.hp:271` | `unsigned long long stdlib_alloc_tm_counter(int index)` | caller index | `index<0 || >=STDLIB_TM_COUNT` → 0 | 0 | none | ALLOC |
| 158 | `stdlib/allocator.hp:287` | `void stdlib_alloc_fail_begin(int mode, long count)` | injection mode | AT/AFTER/else reset | n/a | n/a | ALLOC |
| 159 | `stdlib/allocator.hp:298` | `void stdlib_alloc_fail_end(void)` | injection state | delegates reset | n/a | n/a | ALLOC |
| 160 | `stdlib/allocator.hp:312` | `alloc_fail_scope::alloc_fail_scope(int scope_mode, long scope_count)` | injection mode + count | arms begin | n/a | drop resets | ALLOC |
| 161 | `stdlib/allocator.hp:325` | `void alloc_fail_scope::reset_now(void)` | scope state | idempotent end | n/a | sets armed=0; drop resets again | ALLOC |
| 162 | `stdlib/allocator.hp:331` | `void alloc_fail_scope::drop(void)` | scope state | resets at scope exit | n/a | deterministic cleanup (not thread-safe/nestable) | ALLOC |

### 3.6 Thread synchronization

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 163 | `stdlib/thread.hp:32` | `mutex::mutex(void)` | SYNC handle | `cplus_mutex_new` result | handle NULL → all ops no-op | drop frees if non-NULL | THREAD-D, THREAD-S |
| 164 | `stdlib/thread.hp:40` | `void mutex::lock(void)` | SYNC | NULL handle no-op | void | guard/drop unlock | THREAD-D, THREAD-S |
| 165 | `stdlib/thread.hp:45` | `void mutex::unlock(void)` | SYNC | NULL handle no-op | void | n/a | THREAD-D, THREAD-S |
| 166 | `stdlib/thread.hp:54` | `void mutex::drop(void)` | SYNC handle | frees if non-NULL | n/a | handle=NULL | THREAD-D, THREAD-S |
| 167 | `stdlib/thread.hp:66` | `guard::guard(mutex *owner)` | borrowed mutex | NULL owner → inert; locks if non-NULL | no failure return | drop unlocks once, owner=NULL | THREAD-D, THREAD-S |
| 168 | `stdlib/thread.hp:76` | `void guard::unlock(void)` | borrowed mutex | NULL owner no-op; clears owner after unlock | void | prevents double-unlock in drop | THREAD-D, THREAD-S |
| 169 | `stdlib/thread.hp:83` | `void guard::drop(void)` | borrowed mutex | unlocks iff owner!=NULL | n/a | owner=NULL | THREAD-D, THREAD-S |
| 170 | `stdlib/thread.hp:94` | `condvar::condvar(void)` | SYNC + ALLOC | alloc fail or `pthread_cond_init` fail releases handle | `cv=NULL` | cleanup on init failure | THREAD-S |
| 171 | `stdlib/thread.hp:106` | `int condvar::wait(mutex *m)` | SYNC (external mutex held) | cv/m/m->handle NULL → -1 | -1 on pthread failure | caller must hold m | THREAD-S |
| 172 | `stdlib/thread.hp:114` | `int condvar::signal(void)` | SYNC | NULL cv → -1 | -1 on pthread failure | none | THREAD-S |
| 173 | `stdlib/thread.hp:120` | `int condvar::broadcast(void)` | SYNC | NULL cv → -1 | -1 on pthread failure | none | THREAD-S |
| 174 | `stdlib/thread.hp:126` | `void condvar::drop(void)` | SYNC | destroy + release if non-NULL | n/a | cv=NULL | THREAD-S |
| 175 | `stdlib/thread.hp:148` | `int thread::start(void *(*fn)(void *), void *arg, error *err)` | fn pointer + opaque arg (STR/PTR) | NULL fn, already joinable, `pthread_create` rc | STDLIB_FAIL with err | no thread started on failure | THREAD-D, THREAD-S |
| 176 | `stdlib/thread.hp:174` | `int thread::join(void **out, error *err)` | out pointer | not joinable → LIFECYCLE; `pthread_join` rc | STDLIB_FAIL | joinable=0 on both outcomes | THREAD-D, THREAD-S |
| 177 | `stdlib/thread.hp:191` | `void thread::drop(void)` | joinable thread | joins if joinable | n/a | joinable=0 | THREAD-D, THREAD-S |
| 178 | `stdlib/time.hp:54` | `int time_format_iso8601(unsigned long long unix_ns, char *buf, size_t size, error *err)` | BUF out + timestamp | NULL/0, `secs>LONG_MAX`, `gmtime_r`, `strftime==0` | STDLIB_FAIL; buf[0]='\0' | fixed caller buffer, truncation rejected | TIME |

### 3.7 SDL user input, windows, GUI, media buffers

| # | header:line | signature | boundary / external data | validation present | failure behavior | cleanup behavior | evidence |
|---|---|---|---|---|---|---|---|
| 179 | `stdlib/input.hp:15` | `int poll_event(SDL_Event *event)` | EVENT (borrowed SDL event) | NULL pumps only | 0 when queue empty | caller owns SDL_Event | SDL |
| 180 | `stdlib/input.hp:19` | `int event_is_quit(const SDL_Event *event)` | EVENT | NULL → 0 | 0 | none | SDL |
| 181 | `stdlib/input.hp:23` | `int event_is_key_down(const SDL_Event *event)` | EVENT | NULL → 0 | 0 | none | SDL |
| 182 | `stdlib/input.hp:27` | `int event_is_key_up(const SDL_Event *event)` | EVENT | NULL → 0 | 0 | none | SDL |
| 183 | `stdlib/input.hp:31` | `SDL_Scancode event_scancode(const SDL_Event *event)` | EVENT | NULL/wrong type → UNKNOWN | UNKNOWN | none | SDL |
| 184 | `stdlib/input.hp:38` | `char event_text(const SDL_Event *event)` | EVENT text (STR) | NULL/wrong type/text NULL → '\0' | '\0' | none | SDL |
| 185 | `stdlib/input.hp:46` | `const bool *keyboard_state(void)` | EVENT (borrowed key array) | none; valid until next pump | may return NULL | never free (documented) | SDL |
| 186 | `stdlib/input.hp:50` | `int key_pressed(const bool *state, SDL_Scancode key)` | borrowed key array + key | NULL/out-of-range key → 0 | 0 | none | SDL |
| 187 | `stdlib/input.hp:57` | `unsigned mouse_state(float *x, float *y)` | EVENT (out coords) | SDL writes through x/y (NULL allowed by SDL) | mask | none | SDL |
| 188 | `stdlib/input.hp:61` | `int mouse_button_left(unsigned mask)` | caller mask | mask bit test | 0/1 | none | SDL |
| 189 | `stdlib/window.hp:19` | `sdl::sdl(unsigned flags, error *err)` | caller SDL flags | `SDL_Init` fail → RESOURCE + `SDL_GetError()` | initialized=0 | drop calls `SDL_Quit` | SDL |
| 190 | `stdlib/window.hp:40` | `void sdl::drop(void)` | SDL global state | only quits if initialized | n/a | initialized=0 | SDL |
| 191 | `stdlib/window.hp:55` | `window::window(const char *title, int width, int height, unsigned flags, error *err)` | STR title + dims | width/height>0; `SDL_CreateWindow` fail → RESOURCE | handle NULL | drop destroys owned window | SDL |
| 192 | `stdlib/window.hp:78` | `int window::size(int *width, int *height, error *err)` | out coords | handle/NULL ptrs; `SDL_GetWindowSize` fail → IO | STDLIB_FAIL | none | SDL |
| 193 | `stdlib/window.hp:93` | `int window::set_title(const char *title, error *err)` | STR | handle/title NULL; `SDL_SetWindowTitle` fail → IO | STDLIB_FAIL | none | SDL |
| 194 | `stdlib/window.hp:106` | `void window::drop(void)` | owned window | destroys if non-NULL | n/a | handle=NULL | SDL |
| 195 | `stdlib/gui.hp:37` | `gui::gui(SDL_Window*, SDL_Renderer*, error *err)` | borrowed native handles | NULL handles → UNSUPPORTED; `nk_sdl_init` fail → RESOURCE | context NULL | drop calls `nk_sdl_shutdown` | GUI |
| 196 | `stdlib/gui.hp:70` | `int gui::handle_event(SDL_Event *event)` | EVENT | valid + NULL event → 0 | 0 (not consumed) | none | GUI |
| 197 | `stdlib/gui.hp:81` | `void gui::input_motion(int x, int y)` | EVENT coords | valid check | void | none | GUI |
| 198 | `stdlib/gui.hp:91` | `void gui::input_button(int down)` | EVENT button | valid check; uses stored coords | void | none | GUI |
| 199 | `stdlib/gui.hp:103` | `int gui::window_begin(const char *title, float x, float y, float w, float h, unsigned flags)` | STR title | valid + title NULL → 0 | 0 (skip body) | none | GUI |
| 200 | `stdlib/gui.hp:130` | `int gui::button(const char *label)` | STR label | valid + NULL → 0 | 0 | none | GUI |
| 201 | `stdlib/gui.hp:136` | `void gui::label(const char *text)` | STR | valid + NULL → no-op | void | none | GUI |
| 202 | `stdlib/gui.hp:141` | `int gui::slider(int *value, int minimum, int maximum)` | out value + range | valid, NULL, min>max → 0 | 0 | none | GUI |
| 203 | `stdlib/gui.hp:149` | `int gui::widget_bounds(int *x, int *y, int *width, int *height)` | out coords | current widget; rejects NaN/inf and >INT range | 0 | none | GUI |
| 204 | `stdlib/gui.hp:188` | `void gui::drop(void)` | owned nk context | shuts down if initialized | n/a | context=NULL, initialized=0 | GUI |
| 205 | `stdlib/image.hp:30` | `image::image(const char *path, int desired_channels, error *err)` | PATH + channels | NULL path → empty; channels 0..4; `stbi_load` fail → IO + reason | invalid image, err set | drop `stbi_image_free` | IMAGE |
| 206 | `stdlib/image.hp:84` | `const unsigned char *image::pixel(int x, int y)` | caller coords → BUF | valid + x/y bounds | NULL | borrowed; valid while image lives | IMAGE |
| 207 | `stdlib/image.hp:91` | `int image::write_png(const char *path, error *err)` | PATH + owned pixel BUF | NULL path, empty image, `cols>INT_MAX/channels` stride overflow; write fail → IO | STDLIB_FAIL | none | IMAGE |
| 208 | `stdlib/image.hp:114` | `int image::write_bmp(const char *path, error *err)` | PATH + BUF | NULL path, empty image, write fail → IO | STDLIB_FAIL | none | IMAGE |
| 209 | `stdlib/image.hp:132` | `void image::drop(void)` | owned pixel BUF | frees if non-NULL | n/a | pixels=NULL | IMAGE |
| 210 | `stdlib/image.hp:146` | `image image_from_pixels(const unsigned char *source, int width, int height, int channels, error *err)` | BUF source + dims + ALLOC | NULL/dims<=0 → IO; width*height*channels overflow; malloc fail | invalid image, err set | owns malloc'd buffer; drop frees | IMAGE |
| 211 | `stdlib/image.hp:176` | `image image_from_memory(const unsigned char *data, int size, int desired_channels, error *err)` | BUF data + size | channels 0..4; NULL/size<=0 → IO; decode fail → IO | invalid image, err set | buffer not retained; drop frees decoded pixels | IMAGE |
| 212 | `stdlib/graphics.hp:18` | `surface::surface(SDL_Surface *handle)` | borrowed/owned pixel surface | wraps pointer | invalid if NULL | drop destroys surface if non-NULL | SDL |
| 213 | `stdlib/graphics.hp:35` | `int surface::pixel(int x, int y, unsigned char *out, error *err)` | coords + BUF out | handle/out; x/y bounds; bpp/pitch checks; lock/unlock | STDLIB_FAIL | unlocks surface on all post-lock paths | SDL |
| 214 | `stdlib/graphics.hp:102` | `void surface::drop(void)` | owned surface | destroys if non-NULL | n/a | handle=NULL | SDL |
| 215 | `stdlib/graphics.hp:114` | `renderer::renderer(SDL_Window *native_window, const char *driver, error *err)` | borrowed window + STR driver | NULL window → UNSUPPORTED; create fail → RESOURCE | handle NULL | drop destroys renderer | SDL |
| 216 | `stdlib/graphics.hp:221` | `surface renderer::read(error *err)` | owned snapshot surface | handle NULL → UNSUPPORTED; read fail → IO | empty surface, err set | returned surface must be dropped | SDL |
| 217 | `stdlib/graphics.hp:234` | `void renderer::drop(void)` | owned renderer | destroys if non-NULL | n/a | handle=NULL | SDL |
| 218 | `stdlib/video.hp:21` | `void video_error_text(int status, char *buf, size_t size)` | BUF out | NULL/0 → return; `av_strerror` fail → buf[0]='\0' | void | writes caller buffer | VIDEO |
| 219 | `stdlib/video.hp:31` | `frame::frame(error *err)` | FFmpeg frame ALLOC | `av_frame_alloc` fail → RESOURCE | native_frame NULL | drop `av_frame_free` | VIDEO |
| 220 | `stdlib/video.hp:58` | `void frame::drop(void)` | owned AVFrame | frees if non-NULL | n/a | native_frame=NULL | VIDEO |
| 221 | `stdlib/video.hp:89` | `decoder::decoder(AVFormatContext *owner_format, int stream, error *err)` | borrowed demux context + stream index | NULL/negative; stream params; find/open codec; alloc packet | `initialized=0`, err set | frees codec/packet on partial init; drop frees both | VIDEO |
| 222 | `stdlib/video.hp:151` | `int decoder::next_frame(frame *out, error *err)` | out frame (decoded bytes) | valid/out/frame NULL → FAIL; AVERROR paths; EAGAIN feed; EOF/draining | OK / 1 (EOS) / FAIL(IO) | packet unref before read; flush at EOF | VIDEO |
| 223 | `stdlib/video.hp:194` | `void decoder::drop(void)` | owned codec + packet | frees both when non-NULL | n/a | owner_format=NULL, initialized=0 | VIDEO |
| 224 | `stdlib/video.hp:213` | `media::media(const char *path, error *err)` | PATH | NULL → empty; `avformat_open_input`/`find_stream_info` fail → IO + reason | invalid, err set | closes input on partial init; drop closes | VIDEO |
| 225 | `stdlib/video.hp:286` | `decoder media::open_decoder(error *err)` | borrowed format + stream | delegates to #221 | invalid decoder | caller must drop decoder | VIDEO |
| 226 | `stdlib/video.hp:291` | `void media::drop(void)` | owned AVFormatContext | closes if initialized | n/a | format=NULL, initialized=0 | VIDEO |
| 227 | `stdlib/audio.hp:26` | `audio_context::audio_context(int use_null, error *err)` | caller backend choice | `ma_context_init` rc | initialized=0, err set | drop `ma_context_uninit` | AUDIO |
| 228 | `stdlib/audio.hp:69` | `audio_sound::audio_sound(ma_engine *owner_engine, const char *path, error *err)` | PATH + borrowed engine | NULL engine/path → UNSUPPORTED; `ma_sound_init_from_file` fail → IO | invalid sound | drop `ma_sound_uninit` | AUDIO |
| 229 | `stdlib/audio.hp:90` | `int audio_sound::play(error *err)` | sound state | valid; `ma_sound_start` rc | STDLIB_FAIL IO | none | AUDIO |
| 230 | `stdlib/audio.hp:168` | `int audio_engine::play(const char *path, error *err)` | PATH | valid/path NULL; `ma_engine_play_sound` rc | STDLIB_FAIL IO | none | AUDIO |

**Coverage note.** Rows **1–230** are the document's internal row IDs and each names one
function with an exact `header:line` and signature; the per-header counts in §2 are the
counts of those rows. The **230** figure is the number of functions explicitly inventoried
here, not an estimate and not a claim that no other callable exists in the headers. The
following public functions also consume in-scope data but are grouped under a shared-family
row or a helper note instead of receiving a dedicated row; all are OBSERVED via the cited
alias and DOCUMENTED in the named header (their exact names and `header:line` are also in
`docs/stdlib/REGRESSION_INVENTORY.md §4`):

- AUDIO: `audio_sound::stop:103`, `playing:116`, `audio_engine::load:182`,
  `audio_device::audio_device:199`, `start:232`, `stop:245`, `audio_decoder::audio_decoder:274`,
  `read:311`, `decoder_from_memory:337`.
- ASYNC/HTTP: `loop::valid:33`, `native:38`, `stop:53`, `now_ms:58`, `timer::valid:117`,
  `is_active:150`, `stdlib_timer_release_handle:82`, `sleep_ms:218`, `uv_error_text:222`,
  `http::valid:32`, `http_client::valid:144`, `http_headers::http_headers:51`.
- COL: the remaining scalar-key and checked out-pointer methods of `intmap`, `intset`,
  `deque`, `list`, `stack`, `queue` (`get`, `contains`, `remove`, `pop_*`, `at_checked`,
  `top_checked`, `front_checked`, plus allocation-bearing `grow`/`reserve`/`insert`/`push`
  and `drop`).
- Error/allocator helpers: `stdlib_telemetry_set_enabled:133`, `is_enabled:138`,
  `reset:142`, `stdlib_alloc_fail_at:227`, `fail_after:234`, `fail_reset:241`,
  `stdlib_alloc_seq:249`, `stdlib_alloc_length:499`, `stdlib_safe_div:462`,
  `stdlib_meta_find:273` (the last has no direct call-site; see §5).
- Trivial observers (`valid`, `native`, `size`, metadata getters) are excluded per §0.2.

## 4. OBSERVED register — functions with a test call-site

Of the 230 master-table rows, **228** have at least one call-site. (Two rows — #48 and
#143 — do not; see §5. Four functions total have no direct call-site anywhere in
`tests/stdlib/**/*.cp`, counting two outside the table.) The exact paths are the alias
expansions in §0.4; the most load-bearing examples are:

- **Paths / file I/O**: `tests/stdlib/deep_fs_task5.cp`, `tests/stdlib/deep_file_lifecycle.cp`,
  `tests/stdlib/deep_io_token.cp`, `tests/stdlib/system_safety/fs_symlink_safety.cp`,
  `tests/stdlib/system_safety/io_file_fd_exhaustion.cp`.
- **Network**: `tests/stdlib/deep_net.cp`, `tests/stdlib/http_test.cp`,
  `tests/stdlib/system_safety/net_partial.cp`, `tests/stdlib/regressions/adv_json_fs_net.cp`.
- **JSON**: `tests/stdlib/deep_json_stress.cp`, `tests/stdlib/system_safety/json_numeric_edges.cp`,
  `tests/stdlib/fuzz/f100_json_getters.cp`.
- **Strings / buffers / allocation**: `tests/stdlib/deep_string_task4.cp`,
  `tests/stdlib/deep_memory_bounds.cp`, `tests/stdlib/allocator/allocator_test.cp`,
  `tests/stdlib/allocation_failure/af_string.cp`.
- **Thread sync**: `tests/stdlib/deep_thread_guard.cp`, `tests/stdlib/system_safety/thread_condvar.cp`,
  `tests/stdlib/fuzz/f100_thread_condvar.cp`.
- **SDL input / media / GUI**: `tests/stdlib/sdl_test.cp`, `tests/stdlib/image_test.cp`,
  `tests/stdlib/audio_test.cp`, `tests/stdlib/video_test.cp`, `tests/stdlib/gui_test.cp`
  (all but `image_test`/`audio_test` are conditionally run per §0.5).

Sober reading: an OBSERVED label reflects a **name-level call-site**, the same method whose
caveats are spelled out in `docs/stdlib/REGRESSION_INVENTORY.md §6–7` (unqualified name
matching, implicit `drop()` credits, no branch/execution proof). It is not a safety result.

## 5. UNTESTED register — no test call-site found

Four functions have no direct call-site anywhere in `tests/stdlib/**/*.cp`. Two of them
are master-table rows (#48 and #143); the other two are outside the 230-row table.

| Function | header:line | In 230-row table? | Type | Why no coverage |
|---|---|---|---|---|
| `println` | `stdlib/io.hp:23` | No | public, no external data (out of scope per §0.2) | Only call-site is `tests/stdlib/examples/io_demo.cp`; no assertion test. |
| `http_sink_write` | `stdlib/http.hp:97` | **Yes (#48)** | `static` internal callback | No direct call-site. Reached only indirectly when libcurl invokes it from `http_client::request` (#53); its overflow branch is exercised, if at all, only through the HTTP suite (§0.5). |
| `stdlib_tm_add` | `stdlib/error.hp:127` | **Yes (#143)** | `static inline` internal helper | No direct call-site; invoked indirectly from `stdlib_note`, `stdlib_alloc`, `stdlib_realloc`, etc. |
| `stdlib_meta_find` | `stdlib/error.hp:273` | No (grouped in §3.7 coverage note) | `static inline` internal helper | No direct call-site; invoked indirectly from `stdlib_meta_record`, `stdlib_alloc_size`, generation checks, etc. |

Also with no direct call-site but **not** stdlib API functions: `video_stream_params`
(`stdlib/video.hp:67`, internal helper) and the upstream `stbi_load`/`av_*`/`curl_*`
calls, which are delegated to third-party libraries. No **public** in-scope function
other than the out-of-scope `println` lacks a test call-site.

## 6. Known gaps and caveats carried forward

- `fs_mkdtemp` consumes its `XXXXXX` template on failure — documented, not fixed
  (`docs/stdlib/TASK6_FINAL_REPORT.md §8`). Retry requires reseeding.
- `json_view::at` (`json.hp:121`) relies on yyjson to return NULL for an out-of-range
  index; there is no explicit index guard in the wrapper.
- `stdlib_tm_add` (`error.hp:127`) has no `index` bounds guard; callers pass compile-time
  `STDLIB_TM_*` constants. `stdlib_alloc_tm_counter` does guard its index.
- `stdlib_meta_record` (`error.hp:289`) silently leaves metadata unchanged if the
  table-growth `calloc` fails; entries recorded before the failure remain valid.
- `connection::write` (`net.hp:64`) has no partial-write rollback: bytes already sent
  stay sent when a later write fails.
- `alloc_fail_scope` / `stdlib_alloc_fail_begin` share a single global injection window:
  not nestable, not re-entrant, not thread-safe (`allocator.hp:35–44, 283–306`).
- `std::`/native-library trust is delegated: yyjson, yyjson's `yyjson_read_file`, libcurl,
  libuv, SDL3, FFmpeg, miniaudio, stb, and Nuklear own their own parsing/allocation
  behavior; this document inventories only the C+ wrapper boundary.
- ASan could not be used on the host for most suites; the absence of a sanitizer report
  is not a memory-safety proof (`docs/stdlib/TASK6_FINAL_REPORT.md §6`).

## 7. Reproducing this inventory

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
find stdlib -maxdepth 1 -name '*.hp' | sort
grep -rnE 'getenv|setenv|unsetenv|environ|argv|argc|getopt|system\(|popen|exec[lv]|fork\(' stdlib/
for fn in fs_mkdtemp read_line connection_write json_parse_file stdlib_utf8_validate \
          stdlib_mem_copy stdlib_alloc condvar_wait time_format_iso8601; do
  grep -rlw "$fn" tests/stdlib --include='*.cp'
done
sh tests/stdlib/run_deep_tests.sh
# Conditional suites (see §0.5): sdl_test, gui_test, http_test, async_test, video_test
```

Companion evidence documents: `docs/stdlib/REGRESSION_INVENTORY.md` (static call-site
method and caveats), `docs/stdlib/TASK6_FINAL_REPORT.md` (runner contract, clean-run
result, allocation-failure results, known gaps), `docs/THREAT_MODEL.md`, and
`docs/STDLIB.md`.

# Public API inventory — `stdlib/*.hp` (Agent 2 audit)

> Point-in-time static snapshot, generated 2026-11-23 from branch `agent/modern-stdlib`. No test runner was executed to produce this document. "Deep test reference" means a **static textual match** in the deep-test corpus: it is not execution coverage, not branch/effect verification, and not a correctness or safety claim.

## 1. Measurement method and denominator

1. **Header modulus.** `sorted(Path("stdlib").glob("*.hp"))` — exactly **21 headers** (`async`, `audio`, `collections`, `error`, `file`, `fs`, `graphics`, `gui`, `http`, `image`, `input`, `io`, `json`, `log`, `net`, `string`, `thread`, `time`, `vector`, `video`, `window`).
2. **Callable enumeration.** `tests/stdlib/coverage.py::entries()` was imported and called verbatim on each header. Its parser is `SIG` (definition, optional `static inline`), `CTOR` (constructor), `CLASS` (class scope), with `SKIP = {if, for, while, switch, do, else, return}` plus `main` and all `stdlib_*` helpers excluded. One returned `(name, owning_class)` tuple = one counted public callable (one **entry**).
3. **Signature text.** The "Signature in header" column is the exact source line that the parser matched, stripped of indentation and with its trailing `{` retained. It is not a normalised or hand-written declaration. One entry (`listener.listen` in `net.hp`) is a documented parser artifact — see §2 and §3.14.
4. **Corpus.** **Only** `tests/stdlib/deep_*.cp` (28 files) and `tests/stdlib/deep_*.stdin` whose sibling `.cp` exists (5 files). Non-deep tests, `examples/`, `multi_tu/`, docs and contract prose were not scanned and are not evidence.
5. **Match rules (identical to `coverage.py`).** Literal call = regex `\b<name>\s*\(`; a class `drop` = regex `\b<owning_class>\b` (compiler-inserted destructor, never a literal call); a `*_drop` free function = regex `\b<owner>\b`. Case-sensitive, unqualified, on raw file text (comments and string literals are **not** stripped).
6. **Denominator.** The primary denominator is **381 entries** (the `coverage.py` denominator), because overloads are distinct C++ callable signatures. A secondary bare-name view gives **232 distinct names** (135 referenced, 97 not); the difference (149 entries) is 80 same-header overloads plus 69 names repeated across headers. Because matching is unqualified, one `size(` call marks every `size` entry referenced — an intentional upward bias inherited from `coverage.py`.
7. **Scope of the claim.** This is a static call-site inventory. Entries marked Yes may be reached only through a comment/string token (five such credits are identified in §4), and a Yes never establishes which overload was selected or that the code path ran.

## 2. Reconciliation to the 381-entry denominator

| Quantity | Count | Evidence / how obtained |
|---|---:|---|
| Headers inspected | 21 | `stdlib/*.hp` glob computed in this audit; `docs/stdlib/REGRESSION_INVENTORY.md` §2 |
| Public callable entries | **381** | `coverage.py::entries()` recomputed in this audit (per-header counts below); matches `docs/stdlib/REGRESSION_INVENTORY.md` §3 exactly |
| Entries with a deep-test textual reference | **255** | 234 literal `[L]` + 21 implicit-destructor `[D]` + 0 `*_drop` owner-credit `[O]`; matches `REGRESSION_INVENTORY.md` §4 and `TASK6_FINAL_REPORT.md` §3 |
| Entries with no deep-test reference | **126** | 381 − 255; matches `REGRESSION_INVENTORY.md` §5 |
| Stricter code-only variant | 250 referenced / 131 not | comments and string literals stripped; five credits disappear (`REGRESSION_INVENTORY.md` D7, reproduced in §4 below) |
| Parser artifact inside the 381 | 1 | `listener.listen` is the POSIX `listen(` call at `stdlib/net.hp:120` matched as a definition; there is no `listener::listen` method in the header (§3.14) |

Per-header reconciliation (every header total and every exercised total matches `REGRESSION_INVENTORY.md` §§3–4):

| Header | Entries | Deep-test referenced | Not referenced |
|---|---:|---:|---:|
| `async.hp` | 14 | 5 | 9 |
| `audio.hp` | 29 | 8 | 21 |
| `collections.hp` | 63 | 63 | 0 |
| `error.hp` | 9 | 9 | 0 |
| `file.hp` | 13 | 13 | 0 |
| `fs.hp` | 8 | 8 | 0 |
| `graphics.hp` | 16 | 7 | 9 |
| `gui.hp` | 18 | 2 | 16 |
| `http.hp` | 19 | 5 | 14 |
| `image.hp` | 10 | 1 | 9 |
| `input.hp` | 10 | 0 | 10 |
| `io.hp` | 6 | 5 | 1 |
| `json.hp` | 30 | 29 | 1 |
| `log.hp` | 14 | 12 | 2 |
| `net.hp` | 15 | 14 | 1 |
| `string.hp` | 24 | 24 | 0 |
| `thread.hp` | 21 | 17 | 4 |
| `time.hp` | 5 | 5 | 0 |
| `vector.hp` | 21 | 20 | 1 |
| `video.hp` | 25 | 3 | 22 |
| `window.hp` | 11 | 5 | 6 |
| **Total** | **381** | **255** | **126** |

## 3. Per-header public API inventory

Legend for the last column: `Yes [L]` = literal `name(` token in the deep corpus; `Yes [D]` = owning-class token present, destructor inserted by the compiler (no literal `drop(` call); `No` = no matching token. Qualified names use `Class.method`; free functions and constructors are written as their bare header name.

### 3.1 `async.hp` — 14 entries (5 referenced, 9 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `loop.loop` | `loop(error *err) {` | Yes [L] |
| 2 | `loop.valid` | `int valid(void) {` | Yes [L] |
| 3 | `loop.native` | `uv_loop_t *native(void) {` | Yes [L] |
| 4 | `loop.run` | `int run(int mode, error *err) {` | No |
| 5 | `loop.stop` | `void stop(void) {` | No |
| 6 | `loop.now_ms` | `unsigned long long now_ms(void) {` | No |
| 7 | `loop.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 8 | `timer.timer` | `timer(uv_loop_t *owner_loop, error *err) {` | No |
| 9 | `timer.valid` | `int valid(void) {` | Yes [L] |
| 10 | `timer.stop` | `int stop(error *err) {` | No |
| 11 | `timer.is_active` | `int is_active(void) {` | No |
| 12 | `timer.drop` | `void drop(void) {` | No |
| 13 | `sleep_ms` | `static inline void sleep_ms(unsigned ms) {` | No |
| 14 | `uv_error_text` | `static inline const char *uv_error_text(int status) {` | No |

### 3.2 `audio.hp` — 29 entries (8 referenced, 21 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `audio_context.audio_context` | `audio_context(int use_null, error *err) {` | No |
| 2 | `audio_context.valid` | `int valid(void) {` | Yes [L] |
| 3 | `audio_context.native` | `ma_context *native(void) {` | Yes [L] |
| 4 | `audio_context.drop` | `void drop(void) {` | No |
| 5 | `audio_sound.audio_sound` | `audio_sound(ma_engine *owner_engine, const char *path, error *err) {` | No |
| 6 | `audio_sound.valid` | `int valid(void) {` | Yes [L] |
| 7 | `audio_sound.play` | `int play(error *err) {` | No |
| 8 | `audio_sound.stop` | `int stop(error *err) {` | No |
| 9 | `audio_sound.playing` | `int playing(void) {` | No |
| 10 | `audio_sound.drop` | `void drop(void) {` | No |
| 11 | `audio_engine.audio_engine` | `audio_engine(ma_context *owner_context, error *err) {` | No |
| 12 | `audio_engine.valid` | `int valid(void) {` | Yes [L] |
| 13 | `audio_engine.native` | `ma_engine *native(void) {` | Yes [L] |
| 14 | `audio_engine.set_volume` | `int set_volume(float volume, error *err) {` | No |
| 15 | `audio_engine.play` | `int play(const char *path, error *err) {` | No |
| 16 | `audio_engine.load` | `audio_sound load(const char *path, error *err) {` | No |
| 17 | `audio_engine.drop` | `void drop(void) {` | No |
| 18 | `audio_device.valid` | `int valid(void) {` | Yes [L] |
| 19 | `audio_device.start` | `int start(error *err) {` | Yes [L] |
| 20 | `audio_device.stop` | `int stop(error *err) {` | No |
| 21 | `audio_device.running` | `int running(void) {` | No |
| 22 | `audio_device.drop` | `void drop(void) {` | No |
| 23 | `audio_decoder.audio_decoder` | `audio_decoder(const char *path, error *err) {` | No |
| 24 | `audio_decoder.valid` | `int valid(void) {` | Yes [L] |
| 25 | `audio_decoder.channels` | `unsigned channels(void) {` | No |
| 26 | `audio_decoder.sample_rate` | `unsigned sample_rate(void) {` | No |
| 27 | `audio_decoder.frame_count` | `unsigned long long frame_count(void) {` | No |
| 28 | `audio_decoder.drop` | `void drop(void) {` | No |
| 29 | `audio_sleep_ms` | `static inline void audio_sleep_ms(unsigned ms) {` | No |

### 3.3 `collections.hp` — 63 entries (63 referenced, 0 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `intmap.intmap` | `intmap(void) {` | Yes [L] |
| 2 | `intmap.size` | `size_t size(void) {` | Yes [L] |
| 3 | `intmap.valid` | `int valid(void) {` | Yes [L] |
| 4 | `intmap.grow` | `int grow(size_t want) {` | Yes [L] |
| 5 | `intmap.put` | `int put(int key, int value) {` | Yes [L] |
| 6 | `intmap.get` | `int get(int key, int *out) {` | Yes [L] |
| 7 | `intmap.contains` | `int contains(int key) {` | Yes [L] |
| 8 | `intmap.remove` | `int remove(int key) {` | Yes [L] |
| 9 | `intmap.clear` | `void clear(void) {` | Yes [L] |
| 10 | `intmap.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 11 | `strmap.strmap` | `strmap(void) {` | Yes [L] |
| 12 | `strmap.size` | `size_t size(void) {` | Yes [L] |
| 13 | `strmap.valid` | `int valid(void) {` | Yes [L] |
| 14 | `strmap.grow` | `int grow(size_t want) {` | Yes [L] |
| 15 | `strmap.put` | `int put(const char *key, int value) {` | Yes [L] |
| 16 | `strmap.get` | `int get(const char *key, int *out) {` | Yes [L] |
| 17 | `strmap.contains` | `int contains(const char *key) {` | Yes [L] |
| 18 | `strmap.remove` | `int remove(const char *key) {` | Yes [L] |
| 19 | `strmap.clear` | `void clear(void) {` | Yes [L] |
| 20 | `strmap.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 21 | `intset.intset` | `intset(void) {` | Yes [L] |
| 22 | `intset.size` | `size_t size(void) {` | Yes [L] |
| 23 | `intset.valid` | `int valid(void) {` | Yes [L] |
| 24 | `intset.grow` | `int grow(size_t want) {` | Yes [L] |
| 25 | `intset.insert` | `int insert(int key) {` | Yes [L] |
| 26 | `intset.contains` | `int contains(int key) {` | Yes [L] |
| 27 | `intset.remove` | `int remove(int key) {` | Yes [L] |
| 28 | `intset.clear` | `void clear(void) {` | Yes [L] |
| 29 | `intset.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 30 | `deque.deque` | `deque(void) {` | Yes [L] |
| 31 | `deque.size` | `size_t size(void) {` | Yes [L] |
| 32 | `deque.capacity` | `size_t capacity(void) {` | Yes [L] |
| 33 | `deque.reserve` | `int reserve(size_t want) {` | Yes [L] |
| 34 | `deque.push_back` | `int push_back(int value) {` | Yes [L] |
| 35 | `deque.push_front` | `int push_front(int value) {` | Yes [L] |
| 36 | `deque.pop_front` | `int pop_front(int *out) {` | Yes [L] |
| 37 | `deque.pop_back` | `int pop_back(int *out) {` | Yes [L] |
| 38 | `deque.at_checked` | `int at_checked(size_t index, int *out) {` | Yes [L] |
| 39 | `deque.clear` | `void clear(void) {` | Yes [L] |
| 40 | `deque.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 41 | `list.list` | `list(void) {` | Yes [L] |
| 42 | `list.size` | `size_t size(void) {` | Yes [L] |
| 43 | `list.push_back` | `int push_back(int value) {` | Yes [L] |
| 44 | `list.push_front` | `int push_front(int value) {` | Yes [L] |
| 45 | `list.pop_front` | `int pop_front(int *out) {` | Yes [L] |
| 46 | `list.pop_back` | `int pop_back(int *out) {` | Yes [L] |
| 47 | `list.at_checked` | `int at_checked(size_t index, int *out) {` | Yes [L] |
| 48 | `list.clear` | `void clear(void) {` | Yes [L] |
| 49 | `list.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 50 | `stack.stack` | `stack(void) {` | Yes [L] |
| 51 | `stack.size` | `size_t size(void) {` | Yes [L] |
| 52 | `stack.push` | `int push(int value) {` | Yes [L] |
| 53 | `stack.pop` | `int pop(int *out) {` | Yes [L] |
| 54 | `stack.top_checked` | `int top_checked(int *out) {` | Yes [L] |
| 55 | `stack.clear` | `void clear(void) {` | Yes [L] |
| 56 | `stack.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 57 | `queue.queue` | `queue(void) {` | Yes [L] |
| 58 | `queue.size` | `size_t size(void) {` | Yes [L] |
| 59 | `queue.push` | `int push(int value) {` | Yes [L] |
| 60 | `queue.pop` | `int pop(int *out) {` | Yes [L] |
| 61 | `queue.front_checked` | `int front_checked(int *out) {` | Yes [L] |
| 62 | `queue.clear` | `void clear(void) {` | Yes [L] |
| 63 | `queue.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |

### 3.4 `error.hp` — 9 entries (9 referenced, 0 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `error.error` | `error(void) {` | Yes [L] |
| 2 | `error.ok` | `int ok(void) {` | Yes [L] |
| 3 | `error.failed_now` | `int failed_now(void) {` | Yes [L] |
| 4 | `error.code` | `int code(void) {` | Yes [L] |
| 5 | `error.what` | `const char *what(void) {` | Yes [L] |
| 6 | `error.category` | `int category(void) {` | Yes [L] |
| 7 | `error.clear` | `void clear(void) {` | Yes [L] |
| 8 | `error.set` | `void set(cplus_exit_code code_value, const char *message) {` | Yes [L] |
| 9 | `error_reset` | `static inline void error_reset(void *target) {` | Yes [L] |

### 3.5 `file.hp` — 13 entries (13 referenced, 0 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `file.file` | `file(const char *path, const char *mode) {` | Yes [L] |
| 2 | `file.is_open` | `int is_open(void) {` | Yes [L] |
| 3 | `file.native` | `FILE *native(void) {` | Yes [L] |
| 4 | `file.read` | `int read(char *buf, size_t size, error *err) {` | Yes [L] |
| 5 | `file.read_line` | `int read_line(char *buf, size_t size, error *err) {` | Yes [L] |
| 6 | `file.write` | `int write(const char *text) {` | Yes [L] |
| 7 | `file.write_bytes` | `int write_bytes(const char *data, size_t size, error *err) {` | Yes [L] |
| 8 | `file.seek` | `int seek(long offset, int whence, error *err) {` | Yes [L] |
| 9 | `file.flush` | `int flush(error *err) {` | Yes [L] |
| 10 | `file.close` | `int close(error *err) {` | Yes [L] |
| 11 | `file.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 12 | `file_borrow` | `static inline file file_borrow(FILE *handle) {` | Yes [L] |
| 13 | `file_temp` | `static inline file file_temp(error *err) {` | Yes [L] |

### 3.6 `fs.hp` — 8 entries (8 referenced, 0 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `fs_exists` | `static inline int fs_exists(const char *path) {` | Yes [L] |
| 2 | `fs_is_file` | `static inline int fs_is_file(const char *path) {` | Yes [L] |
| 3 | `fs_is_dir` | `static inline int fs_is_dir(const char *path) {` | Yes [L] |
| 4 | `fs_size` | `static inline long long fs_size(const char *path, error *err) {` | Yes [L] |
| 5 | `fs_remove` | `static inline int fs_remove(const char *path, error *err) {` | Yes [L] |
| 6 | `fs_rename` | `static inline int fs_rename(const char *from, const char *to, error *err) {` | Yes [L] |
| 7 | `fs_mkdir` | `static inline int fs_mkdir(const char *path, error *err) {` | Yes [L] |
| 8 | `fs_mkdtemp` | `static inline int fs_mkdtemp(char *tmpl, size_t size, error *err) {` | Yes [L] |

### 3.7 `graphics.hp` — 16 entries (7 referenced, 9 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `surface.surface` | `surface(SDL_Surface *handle) {` | No |
| 2 | `surface.valid` | `int valid(void) {` | Yes [L] |
| 3 | `surface.width` | `int width(void) {` | No |
| 4 | `surface.height` | `int height(void) {` | No |
| 5 | `surface.pixel` | `int pixel(int x, int y, unsigned char *out, error *err) {` | No |
| 6 | `surface.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 7 | `renderer.renderer` | `renderer(SDL_Window *native_window, const char *driver, error *err) {` | No |
| 8 | `renderer.valid` | `int valid(void) {` | Yes [L] |
| 9 | `renderer.native` | `SDL_Renderer *native(void) {` | Yes [L] |
| 10 | `renderer.clear` | `int clear(error *err) {` | Yes [L] |
| 11 | `renderer.fill_rect` | `int fill_rect(float x, float y, float w, float h, error *err) {` | No |
| 12 | `renderer.line` | `int line(float x1, float y1, float x2, float y2, error *err) {` | Yes [L] |
| 13 | `renderer.present` | `int present(error *err) {` | No |
| 14 | `renderer.set_vsync` | `int set_vsync(int enabled, error *err) {` | No |
| 15 | `renderer.read` | `surface read(error *err) {` | Yes [L] |
| 16 | `renderer.drop` | `void drop(void) {` | No |

### 3.8 `gui.hp` — 18 entries (2 referenced, 16 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `gui.gui` | `gui(SDL_Window *native_window, SDL_Renderer *native_renderer, error *err) {` | No |
| 2 | `gui.valid` | `int valid(void) {` | Yes [L] |
| 3 | `gui.native` | `struct nk_context *native(void) {` | Yes [L] |
| 4 | `gui.handle_event` | `int handle_event(SDL_Event *event) {` | No |
| 5 | `gui.input_begin` | `void input_begin(void) {` | No |
| 6 | `gui.input_motion` | `void input_motion(int x, int y) {` | No |
| 7 | `gui.input_button` | `void input_button(int down) {` | No |
| 8 | `gui.input_end` | `void input_end(void) {` | No |
| 9 | `gui.window_end` | `void window_end(void) {` | No |
| 10 | `gui.layout_row_dynamic` | `void layout_row_dynamic(float height, int columns) {` | No |
| 11 | `gui.layout_row_static` | `void layout_row_static(float height, int item_width, int columns) {` | No |
| 12 | `gui.button` | `int button(const char *label) {` | No |
| 13 | `gui.label` | `void label(const char *text) {` | No |
| 14 | `gui.slider` | `int slider(int *value, int minimum, int maximum) {` | No |
| 15 | `gui.widget_bounds` | `int widget_bounds(int *x, int *y, int *width, int *height) {` | No |
| 16 | `gui.render` | `void render(int anti_aliasing) {` | No |
| 17 | `gui.style_text_color` | `int style_text_color(unsigned char r, unsigned char g, unsigned char b) {` | No |
| 18 | `gui.drop` | `void drop(void) {` | No |

### 3.9 `http.hp` — 19 entries (5 referenced, 14 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `http.http` | `http(error *err) {` | No |
| 2 | `http.valid` | `int valid(void) {` | Yes [L] |
| 3 | `http.drop` | `void drop(void) {` | No |
| 4 | `http_version` | `static inline const char *http_version(void) {` | No |
| 5 | `http_headers.http_headers` | `http_headers(void) {` | No |
| 6 | `http_headers.valid` | `int valid(void) {` | Yes [L] |
| 7 | `http_headers.native` | `struct curl_slist *native(void) {` | Yes [L] |
| 8 | `http_headers.add` | `int add(const char *line, error *err) {` | No |
| 9 | `http_headers.drop` | `void drop(void) {` | No |
| 10 | `http_client.http_client` | `http_client(error *err) {` | No |
| 11 | `http_client.valid` | `int valid(void) {` | Yes [L] |
| 12 | `http_client.set_timeout` | `int set_timeout(long seconds, error *err) {` | No |
| 13 | `http_client.follow_redirects` | `int follow_redirects(int enabled, error *err) {` | No |
| 14 | `http_client.set_headers` | `int set_headers(http_headers *headers, error *err) {` | No |
| 15 | `http_client.header_lines` | `int header_lines(void) {` | No |
| 16 | `http_client.get` | `int get(const char *url, char *buf, size_t size, error *err) {` | Yes [L] |
| 17 | `http_client.status` | `long status(void) {` | No |
| 18 | `http_client.bytes` | `unsigned long long bytes(void) {` | No |
| 19 | `http_client.drop` | `void drop(void) {` | No |

### 3.10 `image.hp` — 10 entries (1 referenced, 9 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `image.image` | `image(const char *path, int desired_channels, error *err) {` | No |
| 2 | `image.valid` | `int valid(void) {` | Yes [L] |
| 3 | `image.width` | `int width(void) {` | No |
| 4 | `image.height` | `int height(void) {` | No |
| 5 | `image.channels` | `int channels(void) {` | No |
| 6 | `image.size_bytes` | `size_t size_bytes(void) {` | No |
| 7 | `image.pixel` | `const unsigned char *pixel(int x, int y) {` | No |
| 8 | `image.write_png` | `int write_png(const char *path, error *err) {` | No |
| 9 | `image.write_bmp` | `int write_bmp(const char *path, error *err) {` | No |
| 10 | `image.drop` | `void drop(void) {` | No |

### 3.11 `input.hp` — 10 entries (0 referenced, 10 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `poll_event` | `static inline int poll_event(SDL_Event *event) {` | No |
| 2 | `event_is_quit` | `static inline int event_is_quit(const SDL_Event *event) {` | No |
| 3 | `event_is_key_down` | `static inline int event_is_key_down(const SDL_Event *event) {` | No |
| 4 | `event_is_key_up` | `static inline int event_is_key_up(const SDL_Event *event) {` | No |
| 5 | `event_scancode` | `static inline SDL_Scancode event_scancode(const SDL_Event *event) {` | No |
| 6 | `event_text` | `static inline char event_text(const SDL_Event *event) {` | No |
| 7 | `keyboard_state` | `static inline const bool *keyboard_state(void) {` | No |
| 8 | `key_pressed` | `static inline int key_pressed(const bool *state, SDL_Scancode key) {` | No |
| 9 | `mouse_state` | `static inline unsigned mouse_state(float *x, float *y) {` | No |
| 10 | `mouse_button_left` | `static inline int mouse_button_left(unsigned mask) {` | No |

### 3.12 `io.hp` — 6 entries (5 referenced, 1 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `println` | `static inline void println(void) {` | No |
| 2 | `read_line` | `static inline int read_line(char *buf, size_t size, error *err) {` | Yes [L] |
| 3 | `read_int` | `static inline int read_int(int *out, error *err) {` | Yes [L] |
| 4 | `read_long` | `static inline int read_long(long *out, error *err) {` | Yes [L] |
| 5 | `read_float` | `static inline int read_float(float *out, error *err) {` | Yes [L] |
| 6 | `read_double` | `static inline int read_double(double *out, error *err) {` | Yes [L] |

### 3.13 `json.hp` — 30 entries (29 referenced, 1 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `json_view.json_view` | `json_view(const yyjson_val *value) {` | No |
| 2 | `json_view.valid` | `int valid(void) {` | Yes [L] |
| 3 | `json_view.is_object` | `int is_object(void) {` | Yes [L] |
| 4 | `json_view.is_array` | `int is_array(void) {` | Yes [L] |
| 5 | `json_view.is_number` | `int is_number(void) {` | Yes [L] |
| 6 | `json_view.is_string` | `int is_string(void) {` | Yes [L] |
| 7 | `json_view.is_bool` | `int is_bool(void) {` | Yes [L] |
| 8 | `json_view.get_int` | `int get_int(int *out, error *err) {` | Yes [L] |
| 9 | `json_view.get_double` | `int get_double(double *out, error *err) {` | Yes [L] |
| 10 | `json_view.get_bool` | `int get_bool(int *out, error *err) {` | Yes [L] |
| 11 | `json_view.get_string` | `int get_string(const char **out, error *err) {` | Yes [L] |
| 12 | `json_view.size` | `size_t size(void) {` | Yes [L] |
| 13 | `json_view.at` | `json_view at(size_t index) {` | Yes [L] |
| 14 | `json_view.member` | `json_view member(const char *key) {` | Yes [L] |
| 15 | `json_view.write` | `char *write(size_t *length) {` | Yes [L] |
| 16 | `json.json` | `json(const char *text, error *err) {` | Yes [L] |
| 17 | `json.valid` | `int valid(void) {` | Yes [L] |
| 18 | `json.root` | `json_view root(void) {` | Yes [L] |
| 19 | `json.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 20 | `json_parse_file` | `static inline json json_parse_file(const char *path, error *err) {` | Yes [L] |
| 21 | `json_view_of` | `static inline json_view json_view_of(const yyjson_val *value) {` | Yes [L] |
| 22 | `json_free` | `static inline void json_free(char *text) {` | Yes [L] |
| 23 | `json_builder.json_builder` | `json_builder(void) {` | Yes [L] |
| 24 | `json_builder.valid` | `int valid(void) {` | Yes [L] |
| 25 | `json_builder.add_int` | `int add_int(const char *key, int value, error *err) {` | Yes [L] |
| 26 | `json_builder.add_double` | `int add_double(const char *key, double value, error *err) {` | Yes [L] |
| 27 | `json_builder.add_bool` | `int add_bool(const char *key, int value, error *err) {` | Yes [L] |
| 28 | `json_builder.add_string` | `int add_string(const char *key, const char *value, error *err) {` | Yes [L] |
| 29 | `json_builder.write` | `char *write(size_t *length) {` | Yes [L] |
| 30 | `json_builder.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |

### 3.14 `log.hp` — 14 entries (12 referenced, 2 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `log_level_name` | `static inline const char *log_level_name(int level) {` | Yes [L] |
| 2 | `log_level_parse` | `static inline int log_level_parse(const char *text, error *err) {` | Yes [L] |
| 3 | `log_sink.log_sink` | `log_sink(FILE *stream, int owns, int threshold) {` | Yes [L] |
| 4 | `log_sink.valid` | `int valid(void) {` | Yes [L] |
| 5 | `log_sink.native` | `FILE *native(void) {` | Yes [L] |
| 6 | `log_sink.level` | `int level(void) {` | Yes [L] |
| 7 | `log_sink.set_level` | `void set_level(int level) {` | Yes [L] |
| 8 | `log_sink.enabled` | `int enabled(int severity) {` | Yes [L] |
| 9 | `log_sink.info` | `int info(const char *message, error *err) {` | Yes [L] |
| 10 | `log_sink.warn` | `int warn(const char *message, error *err) {` | Yes [L] |
| 11 | `log_sink.failure` | `int failure(const char *message, error *err) {` | Yes [L] |
| 12 | `log_sink.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 13 | `log_stderr` | `static inline log_sink log_stderr(int threshold) {` | No |
| 14 | `log_stdout` | `static inline log_sink log_stdout(int threshold) {` | No |

### 3.15 `net.hp` — 15 entries (14 referenced, 1 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `connection.connection` | `connection(int descriptor) {` | No |
| 2 | `connection.is_valid` | `int is_valid(void) {` | Yes [L] |
| 3 | `connection.descriptor` | `int descriptor(void) {` | Yes [L] |
| 4 | `connection.read` | `int read(char *buf, size_t capacity, error *err) {` | Yes [L] |
| 5 | `connection.write` | `int write(const char *data, size_t length, error *err) {` | Yes [L] |
| 6 | `connection.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 7 | `listener.listener` | `listener(int port) {` | Yes [L] |
| 8 | `listener.listen` | `listen(this->fd, 1) == 0) {` | Yes [L]* — artifact, see note |
| 9 | `listener.is_valid` | `int is_valid(void) {` | Yes [L] |
| 10 | `listener.descriptor` | `int descriptor(void) {` | Yes [L] |
| 11 | `listener.bound_port` | `int bound_port(void) {` | Yes [L] |
| 12 | `listener.accept` | `connection accept(void) {` | Yes [L] |
| 13 | `listener.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 14 | `listener_borrow` | `static inline listener listener_borrow(int fd) {` | Yes [L] |
| 15 | `connection_borrow` | `static inline connection connection_borrow(int fd) {` | Yes [L] |

> *Parser artifact:* entry 8 (`listener.listen`) is not a declared method. The `coverage.py` regex matched the body line `listen(this->fd, 1) == 0) {` at `stdlib/net.hp:120` (inside the `listener(int port)` constructor) because its non-greedy prefix consumed `if ` and captured `listen` as the definition name. `stdlib/net.hp` declares no `listener::listen`. The entry is retained so that the table reconciles exactly to the authoritative 381-entry denominator; its `[L]` credit is the POSIX socket `listen(` call, not a stdlib API call.

### 3.16 `string.hp` — 24 entries (24 referenced, 0 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `string_view.string_view` | `string_view(const char *chars, size_t count) {` | Yes [L] |
| 2 | `string_view.size` | `size_t size(void) {` | Yes [L] |
| 3 | `string_view.data` | `const char *data(void) {` | Yes [L] |
| 4 | `string_view.equals` | `int equals(const char *other) {` | Yes [L] |
| 5 | `string_view.is_valid_utf8` | `int is_valid_utf8(void) {` | Yes [L] |
| 6 | `string.string` | `string(const char *text) {` | Yes [L] |
| 7 | `string.size` | `size_t size(void) {` | Yes [L] |
| 8 | `string.capacity` | `size_t capacity(void) {` | Yes [L] |
| 9 | `string.valid` | `int valid(void) {` | Yes [L] |
| 10 | `string.at` | `char at(size_t index) {` | Yes [L] |
| 11 | `string.c_str` | `const char *c_str(void) {` | Yes [L] |
| 12 | `string.equals` | `int equals(const char *other) {` | Yes [L] |
| 13 | `string.compare` | `int compare(const char *other) {` | Yes [L] |
| 14 | `string.find` | `long find(const char *needle) {` | Yes [L] |
| 15 | `string.reserve` | `int reserve(size_t want) {` | Yes [L] |
| 16 | `string.append` | `int append(const char *text) {` | Yes [L] |
| 17 | `string.clear` | `void clear(void) {` | Yes [L] |
| 18 | `string.copy_from` | `int copy_from(string *other) {` | Yes [L] |
| 19 | `string.is_valid_utf8` | `int is_valid_utf8(void) {` | Yes [L] |
| 20 | `string.substr` | `int substr(size_t start, size_t n, char *out, size_t out_size) {` | Yes [L] |
| 21 | `string.count_fields` | `size_t count_fields(char sep) {` | Yes [L] |
| 22 | `string.field` | `int field(char sep, size_t index, char *out, size_t out_size) {` | Yes [L] |
| 23 | `string.to_view` | `string_view to_view(void) {` | Yes [L] |
| 24 | `string.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |

### 3.17 `thread.hp` — 21 entries (17 referenced, 4 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `mutex.mutex` | `mutex(void) {` | Yes [L] |
| 2 | `mutex.is_valid` | `int is_valid(void) {` | Yes [L] |
| 3 | `mutex.lock` | `void lock(void) {` | Yes [L] |
| 4 | `mutex.unlock` | `void unlock(void) {` | Yes [L] |
| 5 | `mutex.native` | `void *native(void) {` | Yes [L] |
| 6 | `mutex.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 7 | `guard.guard` | `guard(mutex *owner) {` | Yes [L] |
| 8 | `guard.holds` | `int holds(void) {` | Yes [L] |
| 9 | `guard.unlock` | `void unlock(void) {` | Yes [L] |
| 10 | `guard.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |
| 11 | `condvar.condvar` | `condvar(void) {` | No |
| 12 | `condvar.is_valid` | `int is_valid(void) {` | Yes [L] |
| 13 | `condvar.wait` | `int wait(mutex *m) {` | No |
| 14 | `condvar.signal` | `int signal(void) {` | Yes [L] |
| 15 | `condvar.broadcast` | `int broadcast(void) {` | No |
| 16 | `condvar.drop` | `void drop(void) {` | No |
| 17 | `thread.thread` | `thread(void) {` | Yes [L] |
| 18 | `thread.is_joinable` | `int is_joinable(void) {` | Yes [L] |
| 19 | `thread.start` | `int start(void *(*fn)(void *), void *arg, error *err) {` | Yes [L] |
| 20 | `thread.join` | `int join(void **out, error *err) {` | Yes [L] |
| 21 | `thread.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |

### 3.18 `time.hp` — 5 entries (5 referenced, 0 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `time_now_ns` | `static inline unsigned long long time_now_ns(void) {` | Yes [L] |
| 2 | `time_now_ms` | `static inline unsigned long long time_now_ms(void) {` | Yes [L] |
| 3 | `time_monotonic_ns` | `static inline unsigned long long time_monotonic_ns(void) {` | Yes [L] |
| 4 | `time_monotonic_ms` | `static inline unsigned long long time_monotonic_ms(void) {` | Yes [L] |
| 5 | `time_sleep_ms` | `static inline void time_sleep_ms(unsigned ms) {` | Yes [L] |

### 3.19 `vector.hp` — 21 entries (20 referenced, 1 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `slice.slice` | `slice(int *items, size_t count) {` | No |
| 2 | `slice.size` | `size_t size(void) {` | Yes [L] |
| 3 | `slice.stale` | `int stale(void) {` | Yes [L] |
| 4 | `slice.valid` | `int valid(void) {` | Yes [L] |
| 5 | `slice.at_checked` | `int at_checked(size_t index, int *out) {` | Yes [L] |
| 6 | `slice.sub` | `slice sub(size_t start, size_t length) {` | Yes [L] |
| 7 | `vector.vector` | `vector(void) {` | Yes [L] |
| 8 | `vector.size` | `size_t size(void) {` | Yes [L] |
| 9 | `vector.capacity` | `size_t capacity(void) {` | Yes [L] |
| 10 | `vector.reserve` | `int reserve(size_t want) {` | Yes [L] |
| 11 | `vector.push_back` | `int push_back(int value) {` | Yes [L] |
| 12 | `vector.at` | `int at(size_t index) {` | Yes [L] |
| 13 | `vector.at_checked` | `int at_checked(size_t index, int *out) {` | Yes [L] |
| 14 | `vector.pop_back` | `int pop_back(int *out) {` | Yes [L] |
| 15 | `vector.insert` | `int insert(size_t index, int value) {` | Yes [L] |
| 16 | `vector.erase` | `int erase(size_t index, int *out) {` | Yes [L] |
| 17 | `vector.clear` | `void clear(void) {` | Yes [L] |
| 18 | `vector.copy_from` | `int copy_from(vector *other) {` | Yes [L] |
| 19 | `vector.shrink_to_fit` | `int shrink_to_fit(void) {` | Yes [L] |
| 20 | `vector.view` | `slice view(void) {` | Yes [L] |
| 21 | `vector.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |

### 3.20 `video.hp` — 25 entries (3 referenced, 22 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `video_error_text` | `static inline void video_error_text(int status, char *buf, size_t size) {` | No |
| 2 | `frame.frame` | `frame(error *err) {` | No |
| 3 | `frame.valid` | `int valid(void) {` | Yes [L] |
| 4 | `frame.width` | `int width(void) {` | No |
| 5 | `frame.height` | `int height(void) {` | No |
| 6 | `frame.format` | `int format(void) {` | No |
| 7 | `frame.pts` | `long long pts(void) {` | No |
| 8 | `frame.drop` | `void drop(void) {` | No |
| 9 | `video_codec_id` | `static inline enum AVCodecID video_codec_id(const AVCodecParameters *params) {` | No |
| 10 | `decoder.decoder` | `decoder(AVFormatContext *owner_format, int stream, error *err) {` | No |
| 11 | `decoder.valid` | `int valid(void) {` | Yes [L] |
| 12 | `decoder.width` | `int width(void) {` | No |
| 13 | `decoder.height` | `int height(void) {` | No |
| 14 | `decoder.next_frame` | `int next_frame(frame *out, error *err) {` | No |
| 15 | `decoder.drop` | `void drop(void) {` | No |
| 16 | `media.media` | `media(const char *path, error *err) {` | No |
| 17 | `media.valid` | `int valid(void) {` | Yes [L] |
| 18 | `media.stream_count` | `int stream_count(void) {` | No |
| 19 | `media.video_index` | `int video_index(void) {` | No |
| 20 | `media.video_width` | `int video_width(void) {` | No |
| 21 | `media.video_height` | `int video_height(void) {` | No |
| 22 | `media.video_codec` | `const char *video_codec(void) {` | No |
| 23 | `media.duration_ms` | `long long duration_ms(void) {` | No |
| 24 | `media.open_decoder` | `decoder open_decoder(error *err) {` | No |
| 25 | `media.drop` | `void drop(void) {` | No |

### 3.21 `window.hp` — 11 entries (5 referenced, 6 not)

| # | Public callable | Signature in header | Deep-test reference |
|---:|---|---|---|
| 1 | `sdl.sdl` | `sdl(unsigned flags, error *err) {` | No |
| 2 | `sdl.valid` | `int valid(void) {` | Yes [L] |
| 3 | `sdl.subsystem_flags` | `unsigned subsystem_flags(void) {` | No |
| 4 | `sdl.drop` | `void drop(void) {` | No |
| 5 | `sdl_backend_error` | `static inline const char *sdl_backend_error(void) {` | No |
| 6 | `window.window` | `window(const char *title, int width, int height, unsigned flags, error *err) {` | No |
| 7 | `window.valid` | `int valid(void) {` | Yes [L] |
| 8 | `window.native` | `SDL_Window *native(void) {` | Yes [L] |
| 9 | `window.size` | `int size(int *width, int *height, error *err) {` | Yes [L] |
| 10 | `window.set_title` | `int set_title(const char *title, error *err) {` | No |
| 11 | `window.drop` | `void drop(void) {` | Yes [D] (implicit destructor) |

## 4. Caveats, approximations and non-claims

- **C1 — entry vs. name.** 381 entries vs. 232 distinct bare names. Both views come from the same parser; the entry view is primary and is the only one that reconciles with `coverage.py` and `REGRESSION_INVENTORY.md`.
- **C2 — unqualified matching.** 149 entries are repeats (80 within-header overloads, 69 cross-header name repeats). A single `size(`, `valid(` or `clear(` call therefore credits several entries; this is a known upward bias of the source method, not a per-signature verification.
- **C3 — constructors and classes.** A class is represented by its constructor entry (`Class.Class`); classes without a constructor entry get no extra entry, and there is no separate `class` entry. This is `coverage.py` behaviour, not an omission.
- **C4 — implicit destructors.** 21 `drop` entries are credited from the owning type token alone: loop.drop, intmap.drop, strmap.drop, intset.drop, deque.drop, list.drop, stack.drop, queue.drop, file.drop, surface.drop, json.drop, json_builder.drop, log_sink.drop, connection.drop, listener.drop, string.drop, mutex.drop, guard.drop, thread.drop, vector.drop, window.drop. Zero entries end in `_drop`, so `[O]` credits are 0.
- **C5 — comment/string-only credits.** Five Yes credits rest on tokens in comments or string literals only: `loop.loop` and `renderer.line` (literal), and `loop.drop`, `surface.drop`, `window.drop` (owner tokens). Stripping comments and strings yields 250 referenced / 131 not, still out of 381. The headline 255 / 126 is the faithful raw-text `coverage.py` result; the stricter figure is disclosed, not substituted.
- **C6 — corpus snapshot.** The corpus is exactly the 28 `.cp` and 5 `.stdin` files listed in `REGRESSION_INVENTORY.md` D8. If the deep corpus changes, every Yes/No cell and the 255 / 126 split must be recomputed.
- **C7 — what this is not.** Not execution coverage, not a branch/path test, not an assertion-strength measure, not an overload-resolution check, and no safety or regression-free claim. No live-game or runtime-success claim follows from this file.

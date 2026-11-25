# Task 6 — Static regression inventory of the deep-test corpus

> **Status: point-in-time static snapshot.** Generated 2026-11-23 04:24 IST from branch `agent/modern-stdlib`.
> This is a **STATIC call-site inventory** — it is **not** execution coverage, a test result, or proof of safety. See §7.

## 1. Method

All numbers below are reproducible and were computed as follows (no test runner was executed):

1. **Header modulus:** `sorted(Path('stdlib').glob('*.hp'))` over the repository root.
2. **Public-callable enumeration:** `tests/stdlib/coverage.py`'s own `entries(path)` function was imported and called verbatim on every header. Its parser is `SIG` (definition with optional `static inline`), `CTOR` (constructor), `CLASS` (class scope), with `SKIP = {if, for, while, switch, do, else, return}` plus `main` and all `stdlib_*` internal helpers excluded. One returned `(name, owning_class)` tuple = one counted public callable (`entry`).
3. **Exercised-name scan corpus:** **only** `tests/stdlib/deep_*.cp` (28 files) plus `tests/stdlib/deep_*.stdin` whose sibling `.cp` exists (5 files). Non-deep tests, `apps/legacy_examples/`, `multi_tu/`, docs, and contract prose were **not** scanned and are **not** evidence.
4. **Match rules** (identical to `coverage.py`): literal call = regex `\b<name>\s*\(`; `drop` on a class = regex `\b<owning_class>\b` (**compiler-inserted destructor**, never a literal call — explicitly flagged in §4 and §6-D5); `*_drop` free function = regex `\b<owner>\b`. Matching is case-sensitive and unqualified, on raw file text.
5. A name counts as exercised **only** if one of those literal/token matches occurs in the corpus; mere mention in documentation or contract prose never counts.

## 2. Modulus — `stdlib/*.hp` headers inspected

**Exact number inspected: 21.**

| # | Header |
|---|--------|
| 1 | `stdlib/async.hp` |
| 2 | `stdlib/audio.hp` |
| 3 | `stdlib/collections.hp` |
| 4 | `stdlib/error.hp` |
| 5 | `stdlib/file.hp` |
| 6 | `stdlib/fs.hp` |
| 7 | `stdlib/graphics.hp` |
| 8 | `stdlib/gui.hp` |
| 9 | `stdlib/http.hp` |
| 10 | `stdlib/image.hp` |
| 11 | `stdlib/input.hp` |
| 12 | `stdlib/io.hp` |
| 13 | `stdlib/json.hp` |
| 14 | `stdlib/log.hp` |
| 15 | `stdlib/net.hp` |
| 16 | `stdlib/string.hp` |
| 17 | `stdlib/thread.hp` |
| 18 | `stdlib/time.hp` |
| 19 | `stdlib/vector.hp` |
| 20 | `stdlib/video.hp` |
| 21 | `stdlib/window.hp` |

## 3. Public API surface (all headers)

**Exact total: 381 public callable entries** across 21 headers (per-header counts below).

| Header | Public callable entries |
|--------|-------------------------|
| `async.hp` | 14 |
| `audio.hp` | 29 |
| `collections.hp` | 63 |
| `error.hp` | 9 |
| `file.hp` | 13 |
| `fs.hp` | 8 |
| `graphics.hp` | 16 |
| `gui.hp` | 18 |
| `http.hp` | 19 |
| `image.hp` | 10 |
| `input.hp` | 10 |
| `io.hp` | 6 |
| `json.hp` | 30 |
| `log.hp` | 14 |
| `net.hp` | 15 |
| `string.hp` | 24 |
| `thread.hp` | 21 |
| `time.hp` | 5 |
| `vector.hp` | 21 |
| `video.hp` | 25 |
| `window.hp` | 11 |
| **Total** | **381** |

## 4. APIs exercised by deep tests

**Exact count: 255 of 381 entries** = 234 by literal call `[L]` + 21 compiler-inserted `drop()` credited by owning type `[D]` + 0 `*_drop` owner-credited `[O]`.

| Header | Exercised |
|--------|-----------|
| `async.hp` | 5 |
| `audio.hp` | 8 |
| `collections.hp` | 63 |
| `error.hp` | 9 |
| `file.hp` | 13 |
| `fs.hp` | 8 |
| `graphics.hp` | 7 |
| `gui.hp` | 2 |
| `http.hp` | 5 |
| `image.hp` | 1 |
| `input.hp` | 0 |
| `io.hp` | 5 |
| `json.hp` | 29 |
| `log.hp` | 12 |
| `net.hp` | 14 |
| `string.hp` | 24 |
| `thread.hp` | 17 |
| `time.hp` | 5 |
| `vector.hp` | 20 |
| `video.hp` | 3 |
| `window.hp` | 5 |
| **Total** | **255** |

### Full list of exercised names (qualified as `Class.method` or free `name`)

`[L]` = literal `name(` call site. `[D]` = **compiler-inserted destructor** `drop()` — credited because the owning type token appears in the corpus, *not* because a call was written. `[O]` = `*_drop` owner-token credit.

**`async.hp` (5):** loop.loop [L], loop.valid [L], loop.native [L], loop.drop [D], timer.valid [L]

**`audio.hp` (8):** audio_context.valid [L], audio_context.native [L], audio_sound.valid [L], audio_engine.valid [L], audio_engine.native [L], audio_device.valid [L], audio_device.start [L], audio_decoder.valid [L]

**`collections.hp` (63):** intmap.intmap [L], intmap.size [L], intmap.valid [L], intmap.grow [L], intmap.put [L], intmap.get [L], intmap.contains [L], intmap.remove [L], intmap.clear [L], intmap.drop [D], strmap.strmap [L], strmap.size [L], strmap.valid [L], strmap.grow [L], strmap.put [L], strmap.get [L], strmap.contains [L], strmap.remove [L], strmap.clear [L], strmap.drop [D], intset.intset [L], intset.size [L], intset.valid [L], intset.grow [L], intset.insert [L], intset.contains [L], intset.remove [L], intset.clear [L], intset.drop [D], deque.deque [L], deque.size [L], deque.capacity [L], deque.reserve [L], deque.push_back [L], deque.push_front [L], deque.pop_front [L], deque.pop_back [L], deque.at_checked [L], deque.clear [L], deque.drop [D], list.list [L], list.size [L], list.push_back [L], list.push_front [L], list.pop_front [L], list.pop_back [L], list.at_checked [L], list.clear [L], list.drop [D], stack.stack [L], stack.size [L], stack.push [L], stack.pop [L], stack.top_checked [L], stack.clear [L], stack.drop [D], queue.queue [L], queue.size [L], queue.push [L], queue.pop [L], queue.front_checked [L], queue.clear [L], queue.drop [D]

**`error.hp` (9):** error.error [L], error.ok [L], error.failed_now [L], error.code [L], error.what [L], error.category [L], error.clear [L], error.set [L], error_reset [L]

**`file.hp` (13):** file.file [L], file.is_open [L], file.native [L], file.read [L], file.read_line [L], file.write [L], file.write_bytes [L], file.seek [L], file.flush [L], file.close [L], file.drop [D], file_borrow [L], file_temp [L]

**`fs.hp` (8):** fs_exists [L], fs_is_file [L], fs_is_dir [L], fs_size [L], fs_remove [L], fs_rename [L], fs_mkdir [L], fs_mkdtemp [L]

**`graphics.hp` (7):** surface.valid [L], surface.drop [D], renderer.valid [L], renderer.native [L], renderer.clear [L], renderer.line [L], renderer.read [L]

**`gui.hp` (2):** gui.valid [L], gui.native [L]

**`http.hp` (5):** http.valid [L], http_headers.valid [L], http_headers.native [L], http_client.valid [L], http_client.get [L]

**`image.hp` (1):** image.valid [L]

**`input.hp` (0):** (none)

**`io.hp` (5):** read_line [L], read_int [L], read_long [L], read_float [L], read_double [L]

**`json.hp` (29):** json_view.valid [L], json_view.is_object [L], json_view.is_array [L], json_view.is_number [L], json_view.is_string [L], json_view.is_bool [L], json_view.get_int [L], json_view.get_double [L], json_view.get_bool [L], json_view.get_string [L], json_view.size [L], json_view.at [L], json_view.member [L], json_view.write [L], json.json [L], json.valid [L], json.root [L], json.drop [D], json_parse_file [L], json_view_of [L], json_free [L], json_builder.json_builder [L], json_builder.valid [L], json_builder.add_int [L], json_builder.add_double [L], json_builder.add_bool [L], json_builder.add_string [L], json_builder.write [L], json_builder.drop [D]

**`log.hp` (12):** log_level_name [L], log_level_parse [L], log_sink.log_sink [L], log_sink.valid [L], log_sink.native [L], log_sink.level [L], log_sink.set_level [L], log_sink.enabled [L], log_sink.info [L], log_sink.warn [L], log_sink.failure [L], log_sink.drop [D]

**`net.hp` (14):** connection.is_valid [L], connection.descriptor [L], connection.read [L], connection.write [L], connection.drop [D], listener.listener [L], listener.listen [L], listener.is_valid [L], listener.descriptor [L], listener.bound_port [L], listener.accept [L], listener.drop [D], listener_borrow [L], connection_borrow [L]

**`string.hp` (24):** string_view.string_view [L], string_view.size [L], string_view.data [L], string_view.equals [L], string_view.is_valid_utf8 [L], string.string [L], string.size [L], string.capacity [L], string.valid [L], string.at [L], string.c_str [L], string.equals [L], string.compare [L], string.find [L], string.reserve [L], string.append [L], string.clear [L], string.copy_from [L], string.is_valid_utf8 [L], string.substr [L], string.count_fields [L], string.field [L], string.to_view [L], string.drop [D]

**`thread.hp` (17):** mutex.mutex [L], mutex.is_valid [L], mutex.lock [L], mutex.unlock [L], mutex.native [L], mutex.drop [D], guard.guard [L], guard.holds [L], guard.unlock [L], guard.drop [D], condvar.is_valid [L], condvar.signal [L], thread.thread [L], thread.is_joinable [L], thread.start [L], thread.join [L], thread.drop [D]

**`time.hp` (5):** time_now_ns [L], time_now_ms [L], time_monotonic_ns [L], time_monotonic_ms [L], time_sleep_ms [L]

**`vector.hp` (20):** slice.size [L], slice.stale [L], slice.valid [L], slice.at_checked [L], slice.sub [L], vector.vector [L], vector.size [L], vector.capacity [L], vector.reserve [L], vector.push_back [L], vector.at [L], vector.at_checked [L], vector.pop_back [L], vector.insert [L], vector.erase [L], vector.clear [L], vector.copy_from [L], vector.shrink_to_fit [L], vector.view [L], vector.drop [D]

**`video.hp` (3):** frame.valid [L], decoder.valid [L], media.valid [L]

**`window.hp` (5):** sdl.valid [L], window.valid [L], window.native [L], window.size [L], window.drop [D]

## 5. Public APIs NOT exercised by any deep test

**Exact count: 126 of 381 entries.** The lists below are the exact unexercised entries per header.

| Header | Not exercised |
|--------|---------------|
| `async.hp` | 9 |
| `audio.hp` | 21 |
| `collections.hp` | 0 |
| `error.hp` | 0 |
| `file.hp` | 0 |
| `fs.hp` | 0 |
| `graphics.hp` | 9 |
| `gui.hp` | 16 |
| `http.hp` | 14 |
| `image.hp` | 9 |
| `input.hp` | 10 |
| `io.hp` | 1 |
| `json.hp` | 1 |
| `log.hp` | 2 |
| `net.hp` | 1 |
| `string.hp` | 0 |
| `thread.hp` | 4 |
| `time.hp` | 0 |
| `vector.hp` | 1 |
| `video.hp` | 22 |
| `window.hp` | 6 |
| **Total** | **126** |

**`async.hp` (9):** loop.run, loop.stop, loop.now_ms, timer.timer, timer.stop, timer.is_active, timer.drop, sleep_ms, uv_error_text

**`audio.hp` (21):** audio_context.audio_context, audio_context.drop, audio_sound.audio_sound, audio_sound.play, audio_sound.stop, audio_sound.playing, audio_sound.drop, audio_engine.audio_engine, audio_engine.set_volume, audio_engine.play, audio_engine.load, audio_engine.drop, audio_device.stop, audio_device.running, audio_device.drop, audio_decoder.audio_decoder, audio_decoder.channels, audio_decoder.sample_rate, audio_decoder.frame_count, audio_decoder.drop, audio_sleep_ms

**`collections.hp` (0):** (none)

**`error.hp` (0):** (none)

**`file.hp` (0):** (none)

**`fs.hp` (0):** (none)

**`graphics.hp` (9):** surface.surface, surface.width, surface.height, surface.pixel, renderer.renderer, renderer.fill_rect, renderer.present, renderer.set_vsync, renderer.drop

**`gui.hp` (16):** gui.gui, gui.handle_event, gui.input_begin, gui.input_motion, gui.input_button, gui.input_end, gui.window_end, gui.layout_row_dynamic, gui.layout_row_static, gui.button, gui.label, gui.slider, gui.widget_bounds, gui.render, gui.style_text_color, gui.drop

**`http.hp` (14):** http.http, http.drop, http_version, http_headers.http_headers, http_headers.add, http_headers.drop, http_client.http_client, http_client.set_timeout, http_client.follow_redirects, http_client.set_headers, http_client.header_lines, http_client.status, http_client.bytes, http_client.drop

**`image.hp` (9):** image.image, image.width, image.height, image.channels, image.size_bytes, image.pixel, image.write_png, image.write_bmp, image.drop

**`input.hp` (10):** poll_event, event_is_quit, event_is_key_down, event_is_key_up, event_scancode, event_text, keyboard_state, key_pressed, mouse_state, mouse_button_left

**`io.hp` (1):** println

**`json.hp` (1):** json_view.json_view

**`log.hp` (2):** log_stderr, log_stdout

**`net.hp` (1):** connection.connection

**`string.hp` (0):** (none)

**`thread.hp` (4):** condvar.condvar, condvar.wait, condvar.broadcast, condvar.drop

**`time.hp` (0):** (none)

**`vector.hp` (1):** slice.slice

**`video.hp` (22):** video_error_text, frame.frame, frame.width, frame.height, frame.format, frame.pts, frame.drop, video_codec_id, decoder.decoder, decoder.width, decoder.height, decoder.next_frame, decoder.drop, media.media, media.stream_count, media.video_index, media.video_width, media.video_height, media.video_codec, media.duration_ms, media.open_decoder, media.drop

**`window.hp` (6):** sdl.sdl, sdl.subsystem_flags, sdl.drop, sdl_backend_error, window.window, window.set_title

## 6. Denominator notes, approximations and ambiguities

- **D1 — `entry` vs. distinct name.** The headline uses `coverage.py`'s `len(entries(...))` = **381 entries**. Collapsing by bare name gives **232 distinct names**, of which 135 are exercised and 97 are not, across the same corpus. Both views are computed from the same parser; the entry view is primary because overloads are distinct callable signatures in C++ and because it matches `coverage.py`'s own denominator.
- **D2 — overloads / repeated names.** There are **149 entries beyond the 232 distinct names**: 80 are extra entries within a single header (overloads such as `size`, `valid`, `clear`, `drop`, `grow`, `write`, `play`, `stop`) and 69 are repeats of the same bare name across different headers (`string.size`, `json_view.size`, `vector.size`, ...). Because matching is by **unqualified name**, a single `size(` call marks every `size` entry exercised — an intentional upward bias inherited verbatim from `coverage.py`.
- **D3 — constructors and classes.** A class is represented by its constructor entry (`Class.Class`), counted once per defined constructor signature. Classes with no constructor entry are not added as separate entries. There is no separate `class` entry — this is `coverage.py`'s behavior, not an omission.
- **D4 — exclusions.** Names beginning `stdlib_` (internal helpers), control-flow keywords in `SKIP`, and `main` are excluded exactly as in `coverage.py`. No other filtering was applied.
- **D5 — compiler-inserted `drop()` (explicit exception).** 21 entries are credited **without any literal `drop(` call**; they are counted because the owning class token appears in the deep corpus (RAII invokes the destructor implicitly). These are: loop.drop, intmap.drop, strmap.drop, intset.drop, deque.drop, list.drop, stack.drop, queue.drop, file.drop, surface.drop, json.drop, json_builder.drop, log_sink.drop, connection.drop, listener.drop, string.drop, mutex.drop, guard.drop, thread.drop, vector.drop, window.drop. Note the caveat in D7 — three of these owner tokens (`loop`, `surface`, `window`) occur only in prose comments, so under a stricter code-only reading they would not qualify.
- **D6 — `*_drop` rule.** The `owner`-token rule was implemented for fidelity but **zero** header entries end in `_drop`, so `[O]` contributes 0 credits.
- **D7 — lexical matching does not strip comments/strings.** `coverage.py` matches raw text. Two literal credits (`loop.loop`, `renderer.line`) and the three `drop` credits noted in D5 rest on comment/string tokens only. A stricter variant with comments and string literals stripped yields **250 exercised / 131 not exercised** (still 381 total). The headline numbers (255 / 126) are the faithful `coverage.py` result; the stricter figure is given for transparency, not substituted.
- **D8 — corpus snapshot.** The sibling agent was adding `deep_*_task5.cp` files concurrently. This snapshot scanned exactly: 28 `deep_*.cp` (`deep_collections_alloc.cp`, `deep_collections_boundary.cp`, `deep_collections_collisions.cp`, `deep_collections_growth.cp`, `deep_cross_family.cp`, `deep_error_task5.cp`, `deep_error_vector.cp`, `deep_file_lifecycle.cp`, `deep_file_lines.cp`, `deep_file_task2.cp`, `deep_fs_task5.cp`, `deep_fs_time.cp`, `deep_io_1byte.cp`, `deep_io_task2.cp`, `deep_io_token.cp`, `deep_json_stress.cp`, `deep_json_task5.cp`, `deep_json_types.cp`, `deep_log_task5.cp`, `deep_net.cp`, `deep_net_task5.cp`, `deep_string_append.cp`, `deep_string_task4.cp`, `deep_string_view.cp`, `deep_thread_guard.cp`, `deep_thread_nested.cp`, `deep_thread_task5.cp`, `deep_vector_task4.cp`) and 5 `deep_*.stdin` (`deep_file_lines.stdin`, `deep_io_1byte.stdin`, `deep_io_task2.stdin`, `deep_io_token.stdin`, `deep_json_stress.stdin`). If the corpus changes, all counts must be recomputed.
- **D9 — no percentages** were computed, rounded, or reported anywhere in this document.
- **D10 — what was excluded as evidence.** Docs (`docs/`), contract prose, shallow tests, `apps/legacy_examples/`, `multi_tu/`, and `coverage.py`'s own `sources()` corpus were deliberately **not** used as evidence for 'exercised'.

## 7. Scope statement — static, not a safety proof

**This is a static call-site inventory only.** It records which public API names have a syntactically matching call/token inside the deep-test corpus. It does **not** mean the code was compiled or run, does not measure executed coverage, does not consider control flow, reachability, assertions, branch outcomes, or which overload was selected, and does **not** prove that a header is correct, safe, hardened, or regression-free. Entries listed as 'exercised' may be exercised in a way that never reaches the code path of interest, and the name-based matching described in D2/D7 can over-credit. No claim of live-game or runtime success follows from this document.

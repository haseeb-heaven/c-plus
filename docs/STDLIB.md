# C+ standard library (`.hp`)

The C+ standard library is a set of `.hp` headers written in C+ itself. It is a
**typed, RAII wrapper layer over libc, POSIX, the C+ runtime and vendored
upstream C libraries** - not a rewrite of any of them.

The surface reads like C++, because that is C+'s model: a type is a noun, you
construct it by naming it, and you call methods on it.

```c
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

int main(void) {
    string name = string("C+");
    name.append(" standard library");

    vector scores = vector();
    scores.push_back(10);
    scores.push_back(20);

    print(name.c_str(), scores.size(), scores.at(1));
    return 0;                     /* both destructors run */
}
```

## Headers

| header | type / API | underlying |
| --- | --- | --- |
| `stdlib/error.hp` | `error` (`ok`, `failed_now`, `code`, `what`, `set`, `clear`) + `STDLIB_OK/EOF/FAIL` | runtime exit codes |
| `stdlib/io.hp` | `read_line`, `read_int`, `read_long`, `read_float`, `read_double`, `println` | `fgets`, `strtol`, `strtoll`, `strtof`, `strtod`, `errno` |
| `stdlib/string.hp` | `string` (`size`, `capacity`, `at`, `c_str`, `equals`, `compare`, `find`, `append`, `reserve`, `clear`, `substr`, `count_fields`, `field`, `to_view`, `drop`), `string_view` (`size`, `data`, `equals`) | `strlen`, `memcmp`, `strstr`, `cplus_alloc` |
| `stdlib/vector.hp` | `vector` (`size`, `capacity`, `reserve`, `push_back`, `pop_back`, `at`, `at_checked`, `clear`, `view`, `drop`) and checked `slice` (`size`, `valid`, `at_checked`, `sub`) | `cplus_alloc` / `cplus_release` |
| `stdlib/file.hp` | `file` (`is_open`, `read`, `read_line`, `write`, `write_bytes`, `seek`, `flush`, `native`, `drop`), `file_borrow` | `fopen`, `fread`, `fgets`, `fputs`, `fwrite`, `fseek`, `fflush`, `fclose` |
| `stdlib/thread.hp` | `mutex` (`is_valid`, `lock`, `unlock`, `native`, `drop`), `guard` (`holds`, `unlock`, `drop`) | runtime mutex wrappers |
| `stdlib/net.hp` | `listener` (`is_valid`, `descriptor`, `bound_port`, `accept`, `drop`), `connection` (`is_valid`, `descriptor`, `read`, `write`, `drop`), `listener_borrow`, `connection_borrow` | POSIX sockets |
| `stdlib/fs.hp` | `fs_exists`, `fs_is_file`, `fs_is_dir`, `fs_size`, `fs_remove`, `fs_rename`, `fs_mkdir` | `stat`, `unlink`, `rename`, `mkdir` |
| `stdlib/time.hp` | `time_now_ns/ms`, `time_monotonic_ns/ms`, `time_sleep_ms`, `time_format_iso8601` | `clock_gettime`, `nanosleep`, `gmtime_r`, `strftime` |
| `stdlib/safe_crt/safe_crt.hp` | checked C API: `cplus_malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `memcpy_s`, `memmove_s`, `strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `sprintf_s`, `asprintf_s`, `fopen_s`, `fread_s`, `fwrite_s`, `scan_line`, `scan_int`, `parse_int`, `parse_double` | libc + contract in `SAFE_CRT.md` |
| `stdlib/json.hp` | `json` (`valid`, `root`, `drop`), `json_view` (`valid`, `is_*`, `get_int`, `get_double`, `get_bool`, `get_string`, `size`, `at`, `member`, `write`), `json_builder` (`valid`, `add_int`, `add_double`, `add_bool`, `add_string`, `write`, `drop`), `json_parse_file`, `json_free` | yyjson 0.13.0 |
| `stdlib/image.hp` | `image` (`valid`, `width`, `height`, `channels`, `size_bytes`, `pixel`, `write_png`, `write_bmp`, `drop`), `image_from_pixels`, `image_from_memory` | stb_image / stb_image_write |
| `stdlib/audio.hp` | `audio_context`, `audio_device`, `audio_engine`, `audio_sound`, `audio_decoder`, `decoder_from_memory`, `audio_sleep_ms` | miniaudio |
| `stdlib/window.hp` | `sdl` (`valid`, `subsystem_flags`, `drop`), `window` (`valid`, `native`, `size`, `set_title`, `drop`) | SDL3 |
| `stdlib/graphics.hp` | `renderer` (`valid`, `native`, `set_color`, `clear`, `fill_rect`, `line`, `present`, `set_vsync`, `read`, `drop`), `surface` (`valid`, `width`, `height`, `pixel`, `drop`) | SDL3 |
| `stdlib/input.hp` | `poll_event`, `event_is_quit`, `event_is_key_down`, `event_is_key_up`, `event_scancode`, `event_text`, `keyboard_state`, `key_pressed`, `mouse_state`, `mouse_button_left` | SDL3 |
| `stdlib/gui.hp` | `gui` (`valid`, `native`, `handle_event`, `input_*`, `window_begin`, `window_end`, `layout_row_dynamic`, `layout_row_static`, `button`, `label`, `slider`, `widget_bounds`, `render`, `style_text_color`, `drop`) | Nuklear + its SDL3 renderer backend |
| `stdlib/http.hp` | `http` (process lifetime), `http_headers` (`add`, `native`, `drop`), `http_client` (`valid`, `set_timeout`, `follow_redirects`, `set_headers`, `get`, `post`, `status`, `bytes`, `drop`) | libcurl |
| `stdlib/async.hp` | `loop` (`valid`, `native`, `run`, `stop`, `now_ms`, `drop`), `timer` (`valid`, `start`, `stop`, `is_active`, `drop`), `read_file`, `sleep_ms`, `uv_error_text` | libuv |
| `stdlib/video.hp` | `media` (`valid`, `stream_count`, `video_index`, `video_width`, `video_height`, `video_codec`, `duration_ms`, `open_decoder`, `drop`), `decoder` (`valid`, `width`, `height`, `next_frame`, `drop`), `frame` (`valid`, `width`, `height`, `format`, `pts`, `drop`), `video_error_text` | FFmpeg |

Ownership rules, the error model, telemetry, vendored-upstream provenance,
coverage and the open compiler gaps are documented in
[`MODERN_STDLIB.md`](MODERN_STDLIB.md).

## Examples

Runnable demos for every library live in `tests/stdlib/examples/` and are built
and run by `tests/stdlib/run_stdlib_tests.sh`.

## Design notes

- Every owning type has a `drop` method and gets deterministic cleanup at scope
  exit, including early returns.
- Recoverable failures (bad input, end of input, missing file, malformed JSON)
  are returned as `error` values with a status code; they never abort.
- Destructors are zero-init safe: a default-constructed handle releases
  nothing, and socket destructors never touch descriptors they do not own.
- Every method logs through `cplus_log`; run `c+ --log trace` (or
  `--log-level trace`) to see the call trace.  With logging off each call
  compiles to nothing.
- Headers are safe to include from several translation units: functions are
  `static inline` and members lowered from a `.hp` get internal linkage.
- The library does not hide C: native handles are reachable with `native()`.
- `.h` files remain fully supported; these headers are additive.

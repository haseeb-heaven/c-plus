# EDGE100 netthread -- API manifest

All callable public APIs declared by the five target headers are enumerated
below and each is exercised by at least one edge case in `harness.cp`.
Constructors count as APIs; free helpers and `static inline` helpers count too.
The `http_sink` struct is a data type, not an API; its callback entry point
`http_sink_write` is listed.

| Header | Public APIs | Cases |
| --- | ---: | ---: |
| `net.hp` | 14 | 32 |
| `http.hp` | 22 | 56 |
| `thread.hp` | 21 | 28 |
| `async.hp` | 18 | 37 |
| `time.hp` | 6 | 16 |
| **total** | **81** | **169** |

## Edge dimensions

Every API is reached through cases drawn from these dimensions:

- **valid** -- well-formed inputs; documented success result and state.
- **NULL** -- null pointer arguments (`error *`, buffers, callbacks, URLs,
  owners, paths) and null/zero-initialised handles.
- **invalid port / URL / header** -- out-of-range ports (`-1`, `65536`,
  `INT_MIN`, `INT_MAX`), malformed HTTP URLs, null/empty/CRLF/control-char
  header lines, invalid libuv run modes.
- **owned vs borrowed** -- `listener_borrow` / `connection_borrow` must not
  close a descriptor the caller owns; `http_client::set_headers` must keep an
  owned copy after the caller frees its `http_headers`; `guard` borrows its mutex.
- **double destroy / join** -- repeated `drop()` on sockets, curl handles,
  libuv loops/timers, pthread wrappers; repeated `join()` and `unlock()`;
  idempotence of all of the above.
- **stale generation** -- use after `drop()`/`join()`, a borrowed fd whose
  underlying descriptor was closed and possibly reused, thread handle reuse
  after join, and client use after `curl_easy_cleanup`.
- **bounded timeouts** -- curl `set_timeout` (0, 1, boundary `2147483`,
  overflow `2147484`, `LONG_MAX`), refused bad URL under a 1 s curl timeout, and
  monotonic-clock assertions that sleeps/joins/waits return within 2 s.
- **epoch boundaries** -- `time_format_iso8601` at 0, sub-second rollover,
  1 s rollover, a fixed UTC instant, `ULLONG_MAX`, exact 21-byte buffer,
  20-byte refusal, plus port/buffer/timer extremes throughout.

## Per-API coverage

### `net.hp` (14 APIs)

| # | API | Cases | Case names |
| ---: | --- | ---: | --- |
| 1 | `net.connection::connection` | 3 | `conn_ctor_valid`, `conn_ctor_invalid`, `conn_zero_init` |
| 2 | `net.connection::is_valid` | 1 | `conn_isvalid_borrowed` |
| 3 | `net.connection::descriptor` | 1 | `conn_descriptor` |
| 4 | `net.connection::read` | 6 | `conn_read_valid`, `conn_read_eof`, `conn_read_null`, `conn_read_zerocap`, `conn_read_invalid`, `conn_read_stale` |
| 5 | `net.connection::write` | 5 | `conn_write_valid`, `conn_write_null`, `conn_write_zerolen`, `conn_write_invalid`, `conn_write_stale` |
| 6 | `net.connection::drop` | 1 | `conn_drop_double` |
| 7 | `net.connection_borrow` | 2 | `conn_borrow_fd`, `conn_borrow_stale_reuse` |
| 8 | `net.listener::listener` | 4 | `lsn_ctor_valid`, `lsn_ctor_invalid`, `lsn_zero_init`, `lsn_port_max` |
| 9 | `net.listener::is_valid` | 1 | `lsn_isvalid_borrowed` |
| 10 | `net.listener::descriptor` | 1 | `lsn_descriptor` |
| 11 | `net.listener::bound_port` | 2 | `lsn_bound_port`, `lsn_bound_port_invalid` |
| 12 | `net.listener::accept` | 2 | `lsn_accept_valid`, `lsn_accept_invalid` |
| 13 | `net.listener::drop` | 1 | `lsn_drop_double` |
| 14 | `net.listener_borrow` | 2 | `lsn_borrow_fd`, `lsn_borrow_stale` |

### `http.hp` (22 APIs)

| # | API | Cases | Case names |
| ---: | --- | ---: | --- |
| 1 | `http.http::http` | 2 | `http_ctor_valid`, `http_null_err` |
| 2 | `http.http::drop` | 1 | `http_drop_double` |
| 3 | `http.http_version` | 1 | `http_version` |
| 4 | `http.http_headers::http_headers` | 1 | `headers_ctor_valid` |
| 5 | `http.http_headers::add` | 4 | `headers_add_null`, `headers_add_valid`, `headers_add_hostile`, `headers_add_empty` |
| 6 | `http.http_headers::native` | 1 | `headers_native` |
| 7 | `http.http_headers::drop` | 1 | `headers_drop_double` |
| 8 | `http.http_sink_write` | 12 | `sink_null_userdata`, `sink_null_buf`, `sink_zero_size`, `sink_zero_bytes`, `sink_null_ptr`, `sink_mul_overflow`, `sink_valid_write`, `sink_exact_fit`, `sink_oversize_clamp`, `sink_full_refuse`, `sink_used_sizemax`, `sink_boundary_nul` |
| 9 | `http.http_client::http_client` | 2 | `client_ctor_valid`, `client_null_err` |
| 10 | `http.http_client::set_timeout` | 5 | `client_set_timeout_neg`, `client_set_timeout_zero`, `client_set_timeout_valid`, `client_set_timeout_max`, `client_set_timeout_after_drop` |
| 11 | `http.http_client::follow_redirects` | 3 | `client_follow_0`, `client_follow_1`, `client_follow_after_drop` |
| 12 | `http.http_client::set_headers` | 4 | `client_headers_null`, `client_headers_empty`, `client_headers_owned`, `client_headers_after_drop` |
| 13 | `http.http_client::header_lines` | 1 | `client_header_lines_fresh` |
| 14 | `http.http_client::status` | 2 | `client_status_fresh`, `client_status_after_drop` |
| 15 | `http.http_client::bytes` | 2 | `client_bytes_fresh`, `client_bytes_after_drop` |
| 16 | `http.http_client::get` | 6 | `client_get_null_url`, `client_get_null_buf`, `client_get_zero_size`, `client_get_after_drop`, `client_get_bad_url_timeout`, `client_get_file_ok` |
| 17 | `http.http_client::post` | 3 | `client_post_null_body`, `client_post_file_ok`, `client_post_null_url` |
| 18 | `http.http_client::request` | 1 | `client_request_direct` |
| 19 | `http.http_client::drop` | 1 | `client_drop_double` |
| 20 | `http.http::valid` | 1 | `http_valid` |
| 21 | `http.http_headers::valid` | 1 | `headers_valid` |
| 22 | `http.http_client::valid` | 1 | `client_valid` |

### `thread.hp` (21 APIs)

| # | API | Cases | Case names |
| ---: | --- | ---: | --- |
| 1 | `thread.mutex::mutex` | 1 | `mutex_ctor_valid` |
| 2 | `thread.mutex::is_valid` | 1 | `mutex_zero_init` |
| 3 | `thread.mutex::lock` | 1 | `mutex_lock_unlock` |
| 4 | `thread.mutex::native` | 1 | `mutex_native` |
| 5 | `thread.mutex::drop` | 1 | `mutex_drop_double` |
| 6 | `thread.guard::guard` | 2 | `guard_ctor_valid`, `guard_null` |
| 7 | `thread.guard::holds` | 1 | `guard_zero_init` |
| 8 | `thread.guard::unlock` | 1 | `guard_unlock_double` |
| 9 | `thread.guard::drop` | 1 | `guard_drop_double` |
| 10 | `thread.condvar::condvar` | 1 | `condvar_ctor_valid` |
| 11 | `thread.condvar::is_valid` | 1 | `condvar_zero_init` |
| 12 | `thread.condvar::wait` | 2 | `condvar_wait_null`, `condvar_wait_signal` |
| 13 | `thread.condvar::signal` | 1 | `condvar_signal_broadcast` |
| 14 | `thread.condvar::drop` | 1 | `condvar_drop_double` |
| 15 | `thread.thread::thread` | 1 | `thread_ctor_zero` |
| 16 | `thread.thread::start` | 4 | `thread_start_null_fn`, `thread_start_valid`, `thread_start_twice`, `thread_generation_reuse` |
| 17 | `thread.thread::join` | 3 | `thread_join_before_start`, `thread_join_double`, `thread_bounded_join` |
| 18 | `thread.thread::drop` | 1 | `thread_drop_joinable` |
| 19 | `thread.mutex::unlock` | 1 | `mutex_unlock` |
| 20 | `thread.condvar::broadcast` | 1 | `condvar_broadcast` |
| 21 | `thread.thread::is_joinable` | 1 | `thread_is_joinable` |

### `async.hp` (18 APIs)

| # | API | Cases | Case names |
| ---: | --- | ---: | --- |
| 1 | `async.stdlib_uv_run_mode_valid` | 2 | `uv_mode_valid_0_1_2`, `uv_mode_invalid` |
| 2 | `async.loop::loop` | 2 | `loop_ctor_valid`, `loop_ctor_null_err` |
| 3 | `async.loop::valid` | 1 | `loop_zero_init` |
| 4 | `async.loop::native` | 1 | `loop_native` |
| 5 | `async.loop::run` | 3 | `loop_run_valid`, `loop_run_invalid_mode`, `loop_run_invalid_loop` |
| 6 | `async.loop::stop` | 1 | `loop_stop` |
| 7 | `async.loop::now_ms` | 1 | `loop_now_ms` |
| 8 | `async.loop::drop` | 1 | `loop_drop_double` |
| 9 | `async.timer::timer` | 2 | `timer_ctor_null_loop`, `timer_ctor_valid` |
| 10 | `async.timer::valid` | 1 | `timer_zero_init` |
| 11 | `async.timer::start` | 4 | `timer_start_null_cb`, `timer_start_valid`, `timer_start_extremes`, `timer_start_after_drop` |
| 12 | `async.timer::stop` | 2 | `timer_stop_invalid`, `timer_stop_valid` |
| 13 | `async.timer::is_active` | 1 | `timer_is_active` |
| 14 | `async.timer::drop` | 1 | `timer_drop_double` |
| 15 | `async.read_file` | 10 | `read_file_null_owner`, `read_file_invalid_owner`, `read_file_null_path`, `read_file_null_buf`, `read_file_zero_size`, `read_file_size_bounds`, `read_file_missing`, `read_file_devnull`, `read_file_fixture`, `read_file_partial` |
| 16 | `async.sleep_ms` | 2 | `sleep_ms_zero`, `sleep_ms_bounded` |
| 17 | `async.uv_error_text` | 1 | `uv_error_text` |
| 18 | `async.stdlib_timer_release_handle` | 1 | `timer_release` |

### `time.hp` (6 APIs)

| # | API | Cases | Case names |
| ---: | --- | ---: | --- |
| 1 | `time.time_now_ns` | 1 | `time_now_ns_ms` |
| 2 | `time.time_now_ms` | 1 | `time_now_ns_ms` |
| 3 | `time.time_monotonic_ns` | 2 | `time_monotonic_ns_ms`, `time_monotonic_order` |
| 4 | `time.time_monotonic_ms` | 1 | `time_monotonic_ns_ms` |
| 5 | `time.time_sleep_ms` | 2 | `time_sleep_zero`, `time_sleep_bounded` |
| 6 | `time.time_format_iso8601` | 9 | `time_iso_epoch`, `time_iso_subsec`, `time_iso_sec_boundary`, `time_iso_far_epoch`, `time_iso_null_buf`, `time_iso_zero_size`, `time_iso_small_buf`, `time_iso_exact_buf`, `time_iso_null_err` |


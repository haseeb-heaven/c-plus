# EDGE100 multimedia — API manifest

Every public callable of the seven assigned headers is enumerated here.
The list is generated from `harness list` (the same table the harness dispatches),
so the manifest cannot drift from the executed cases.

- **APIs covered:** 127 (distinct `header method` pairs)
- **Cases executed:** 173 (fork-per-case, each bounded to 60 s)
- **Edge tags:** `valid` (happy path), `null` (NULL/invalid arguments),
  `dim` / `dimbig` (zero/negative/oversized dimensions or flags), `fmt` (invalid
  format or driver), `trunc` (truncated media), `drop2` (double-drop),
  `oob` (out-of-range coordinates/indices). Not every category is meaningful for
  every API; the applicable ones are listed per API.

| # | Header | API | Header line | Edge cases (tag @ case id) |
|---:|---|---|---|---|
| 1 | `window.hp` | `sdl::ctor` | 19 | `valid` @0, `null` @1, `dim` @2 |
| 2 | `window.hp` | `sdl::valid` | 32 | `valid` @3 |
| 3 | `window.hp` | `sdl::subsystem_flags` | 36 | `valid` @4, `dim` @5 |
| 4 | `window.hp` | `sdl_backend_error` | 48 | `valid` @6 |
| 5 | `window.hp` | `sdl::drop` | 40 | `drop2` @7 |
| 6 | `window.hp` | `window::ctor` | 55 | `valid` @8, `null` @9, `dim` @10, `dimbig` @11 |
| 7 | `window.hp` | `window::valid` | 32 | `valid` @12 |
| 8 | `window.hp` | `window::native` | 74 | `valid` @13 |
| 9 | `window.hp` | `window::size` | 78 | `valid` @14, `null` @15 |
| 10 | `window.hp` | `window::set_title` | 93 | `valid` @16, `null` @17 |
| 11 | `window.hp` | `window::drop` | 40 | `valid` @18, `drop2` @19 |
| 12 | `graphics.hp` | `renderer::ctor` | 114 | `valid` @20, `null` @21, `fmt` @22 |
| 13 | `graphics.hp` | `renderer::valid` | 22 | `valid` @23 |
| 14 | `graphics.hp` | `renderer::native` | 132 | `valid` @24 |
| 15 | `graphics.hp` | `renderer::set_color` | 136 | `valid` @25 |
| 16 | `graphics.hp` | `renderer::clear` | 150 | `valid` @26 |
| 17 | `graphics.hp` | `renderer::fill_rect` | 163 | `oob` @27 |
| 18 | `graphics.hp` | `renderer::line` | 181 | `oob` @28 |
| 19 | `graphics.hp` | `renderer::set_vsync` | 207 | `valid` @29 |
| 20 | `graphics.hp` | `renderer::present` | 194 | `valid` @30 |
| 21 | `graphics.hp` | `renderer::read` | 221 | `valid` @31 |
| 22 | `graphics.hp` | `renderer::drop` | 102 | `valid` @32, `drop2` @33 |
| 23 | `graphics.hp` | `surface::ctor` | 18 | `valid` @34 |
| 24 | `graphics.hp` | `surface::valid` | 22 | `valid` @35 |
| 25 | `graphics.hp` | `surface::width` | 26 | `valid` @36 |
| 26 | `graphics.hp` | `surface::height` | 30 | `valid` @37 |
| 27 | `graphics.hp` | `surface::pixel` | 35 | `valid` @38, `oob` @39, `null` @40 |
| 28 | `graphics.hp` | `surface::drop` | 102 | `valid` @41, `drop2` @42 |
| 29 | `input.hp` | `poll_event` | 15 | `valid` @43, `null` @44 |
| 30 | `input.hp` | `event_is_quit` | 19 | `null` @45 |
| 31 | `input.hp` | `event_is_key_down` | 23 | `null` @46 |
| 32 | `input.hp` | `event_is_key_up` | 27 | `null` @47 |
| 33 | `input.hp` | `event_scancode` | 31 | `null` @48 |
| 34 | `input.hp` | `event_text` | 38 | `null` @49 |
| 35 | `input.hp` | `keyboard_state` | 46 | `valid` @50 |
| 36 | `input.hp` | `key_pressed` | 50 | `oob` @51 |
| 37 | `input.hp` | `mouse_state` | 57 | `valid` @52 |
| 38 | `input.hp` | `mouse_button_left` | 61 | `valid` @53 |
| 39 | `image.hp` | `image_from_pixels` | 146 | `valid` @54, `null` @55, `dim` @56, `dimbig` @57 |
| 40 | `image.hp` | `image::ctor` | 30 | `valid` @58, `null` @59, `fmt` @60, `trunc` @61, `dim` @62 |
| 41 | `image.hp` | `image::valid` | 59 | `valid` @63 |
| 42 | `image.hp` | `image::width` | 64 | `valid` @64 |
| 43 | `image.hp` | `image::height` | 68 | `valid` @65 |
| 44 | `image.hp` | `image::channels` | 72 | `valid` @66 |
| 45 | `image.hp` | `image::size_bytes` | 76 | `valid` @67 |
| 46 | `image.hp` | `image::pixel` | 84 | `valid` @68, `oob` @69 |
| 47 | `image.hp` | `image::write_png` | 91 | `null` @70 |
| 48 | `image.hp` | `image::write_bmp` | 114 | `null` @71 |
| 49 | `image.hp` | `image::drop` | 132 | `valid` @72, `drop2` @73 |
| 50 | `image.hp` | `image_from_memory` | 176 | `valid` @74, `null` @75, `trunc` @76, `fmt` @77 |
| 51 | `audio.hp` | `audio_sleep_ms` | 355 | `valid` @78 |
| 52 | `audio.hp` | `decoder_from_memory` | 337 | `valid` @79, `null` @80, `trunc` @81 |
| 53 | `audio.hp` | `audio_decoder::ctor` | 274 | `valid` @82, `null` @83, `trunc` @84, `fmt` @85 |
| 54 | `audio.hp` | `audio_decoder::valid` | 47 | `valid` @86 |
| 55 | `audio.hp` | `audio_decoder::channels` | 293 | `valid` @87 |
| 56 | `audio.hp` | `audio_decoder::sample_rate` | 297 | `valid` @88 |
| 57 | `audio.hp` | `audio_decoder::frame_count` | 301 | `valid` @89 |
| 58 | `audio.hp` | `audio_decoder::read` | 311 | `null` @90 |
| 59 | `audio.hp` | `audio_decoder::drop` | 56 | `valid` @91, `drop2` @92 |
| 60 | `audio.hp` | `audio_context::ctor` | 26 | `valid` @93, `null` @94 |
| 61 | `audio.hp` | `audio_context::valid` | 47 | `valid` @95 |
| 62 | `audio.hp` | `audio_context::native` | 52 | `valid` @96 |
| 63 | `audio.hp` | `audio_context::drop` | 56 | `valid` @97, `drop2` @98 |
| 64 | `audio.hp` | `audio_engine::ctor` | 134 | `valid` @99, `null` @100 |
| 65 | `audio.hp` | `audio_engine::valid` | 47 | `valid` @101 |
| 66 | `audio.hp` | `audio_engine::native` | 52 | `valid` @102 |
| 67 | `audio.hp` | `audio_engine::set_volume` | 157 | `oob` @103 |
| 68 | `audio.hp` | `audio_engine::play` | 90 | `null` @104 |
| 69 | `audio.hp` | `audio_engine::load` | 182 | `null` @105 |
| 70 | `audio.hp` | `audio_engine::drop` | 56 | `valid` @106, `drop2` @107 |
| 71 | `audio.hp` | `audio_sound::ctor` | 69 | `null` @108 |
| 72 | `audio.hp` | `audio_sound::valid` | 47 | `valid` @109 |
| 73 | `audio.hp` | `audio_sound::play` | 90 | `null` @110 |
| 74 | `audio.hp` | `audio_sound::stop` | 103 | `valid` @111 |
| 75 | `audio.hp` | `audio_sound::playing` | 116 | `valid` @112 |
| 76 | `audio.hp` | `audio_sound::drop` | 56 | `valid` @113, `drop2` @114 |
| 77 | `audio.hp` | `audio_device::ctor` | 199 | `valid` @115, `null` @116, `dim` @117 |
| 78 | `audio.hp` | `audio_device::valid` | 47 | `valid` @118 |
| 79 | `audio.hp` | `audio_device::start` | 232 | `valid` @119 |
| 80 | `audio.hp` | `audio_device::running` | 258 | `valid` @120 |
| 81 | `audio.hp` | `audio_device::stop` | 103 | `valid` @121 |
| 82 | `audio.hp` | `audio_device::drop` | 56 | `valid` @122, `drop2` @123 |
| 83 | `gui.hp` | `gui::ctor` | 37 | `null` @124 |
| 84 | `gui.hp` | `gui::valid` | 60 | `valid` @125 |
| 85 | `gui.hp` | `gui::native` | 65 | `valid` @126 |
| 86 | `gui.hp` | `gui::handle_event` | 70 | `null` @127 |
| 87 | `gui.hp` | `gui::input_begin` | 76 | `valid` @128 |
| 88 | `gui.hp` | `gui::input_motion` | 81 | `oob` @129 |
| 89 | `gui.hp` | `gui::input_button` | 91 | `valid` @130 |
| 90 | `gui.hp` | `gui::input_end` | 97 | `valid` @131 |
| 91 | `gui.hp` | `gui::window_begin` | 103 | `null` @132 |
| 92 | `gui.hp` | `gui::window_end` | 113 | `valid` @133 |
| 93 | `gui.hp` | `gui::layout_row_dynamic` | 120 | `dim` @134 |
| 94 | `gui.hp` | `gui::layout_row_static` | 125 | `dim` @135 |
| 95 | `gui.hp` | `gui::button` | 93 | `null` @136 |
| 96 | `gui.hp` | `gui::label` | 136 | `null` @137 |
| 97 | `gui.hp` | `gui::slider` | 141 | `oob` @138 |
| 98 | `gui.hp` | `gui::widget_bounds` | 149 | `null` @139 |
| 99 | `gui.hp` | `gui::render` | 175 | `valid` @140 |
| 100 | `gui.hp` | `gui::style_text_color` | 181 | `valid` @141 |
| 101 | `gui.hp` | `gui::drop` | 188 | `valid` @142, `drop2` @143 |
| 102 | `video.hp` | `video_error_text` | 21 | `null` @144 |
| 103 | `video.hp` | `video_stream_params` | 67 | `oob` @145 |
| 104 | `video.hp` | `video_codec_id` | 76 | `null` @146 |
| 105 | `video.hp` | `decoder::ctor` | 89 | `null` @147 |
| 106 | `video.hp` | `media::ctor` | 213 | `trunc` @148 |
| 107 | `video.hp` | `media::valid` | 38 | `valid` @149 |
| 108 | `video.hp` | `media::stream_count` | 248 | `valid` @150 |
| 109 | `video.hp` | `media::video_index` | 252 | `valid` @151 |
| 110 | `video.hp` | `media::video_width` | 256 | `valid` @152 |
| 111 | `video.hp` | `media::video_height` | 263 | `valid` @153 |
| 112 | `video.hp` | `media::video_codec` | 270 | `valid` @154 |
| 113 | `video.hp` | `media::duration_ms` | 279 | `valid` @155 |
| 114 | `video.hp` | `media::open_decoder` | 286 | `null` @156 |
| 115 | `video.hp` | `decoder::valid` | 38 | `valid` @157 |
| 116 | `video.hp` | `decoder::width` | 42 | `valid` @158 |
| 117 | `video.hp` | `decoder::height` | 46 | `valid` @159 |
| 118 | `video.hp` | `decoder::next_frame` | 151 | `null` @160 |
| 119 | `video.hp` | `frame::ctor` | 31 | `null` @161 |
| 120 | `video.hp` | `frame::valid` | 38 | `valid` @162 |
| 121 | `video.hp` | `frame::width` | 42 | `valid` @163 |
| 122 | `video.hp` | `frame::height` | 46 | `valid` @164 |
| 123 | `video.hp` | `frame::format` | 50 | `valid` @165 |
| 124 | `video.hp` | `frame::pts` | 54 | `valid` @166 |
| 125 | `video.hp` | `frame::drop` | 58 | `valid` @167, `drop2` @168 |
| 126 | `video.hp` | `decoder::drop` | 58 | `valid` @169, `drop2` @170 |
| 127 | `video.hp` | `media::drop` | 58 | `valid` @171, `drop2` @172 |

## Coverage by header

| Header | APIs | Cases |
|---|---:|---:|
| `window.hp` | 11 | 20 |
| `graphics.hp` | 17 | 23 |
| `input.hp` | 10 | 11 |
| `image.hp` | 12 | 24 |
| `audio.hp` | 32 | 46 |
| `gui.hp` | 19 | 20 |
| `video.hp` | 26 | 29 |
| **total** | **127** | **173** |

## Notes

- Include order and per-header flags mirror `tests/stdlib/method_tests/multimedia/run_all.sh`;
  all seven headers are compiled into one `--legacy-c` translation unit.
- Because the C+ `--legacy-c` rewriter binds variable names to class types file-wide,
  class-typed locals use type-prefixed names (e.g. `md_m` for `media`, `fr_f` for `frame`).
- FFmpeg and Nuklear emit benign stderr diagnostics for intentionally truncated input;
  those do not affect the PASS/FAIL tally (only the final line of each case is counted).

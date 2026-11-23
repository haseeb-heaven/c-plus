# Safety-refusal terminal screenshots — manifest

**What these are.** The files in this directory are **rendered images of captured
real terminal output**, stored under `examples/` per request. They are
**evidence of tested runtime refusals, not a full safety proof**: every `OK` /
`REFUSED` line is an observation of a guarded API call during a real execution,
not a claim that the checked layers establish pointer provenance, ownership,
alias/UAF freedom, or thread safety.

**How they were made.** A capture step built each real demo with the exact flags
below and ran the resulting binary under `/usr/bin/timeout 30`, recording
stdout+stderr verbatim into `examples/screenshots/captures/*.txt`. `render.sh`
(POSIX sh + awk, no Python) reads those transcripts, keeps only the lines between
the `# --- BEGIN/END VERBATIM OUTPUT ---` markers, XML-escapes them, highlights
standalone `REFUSED` / `OK` tokens in green, and emits the SVG terminal
screenshots. Each SVG also carries a `<desc>` with the verbatim lines so the
exact captured text remains greppable.

**Reproduce:** `cd examples/screenshots/images && ./render.sh`
(override the transcript directory with `CAPTURES_DIR=/path ./render.sh`).
Verbatim fallback copies of the four primary transcripts are kept in
`examples/screenshots/images/captures/` so rendering still works from a clean
state if the sibling capture directory is absent.

**Toolchain (from every transcript header):** `C+ 0.1.0 ; Apple clang version
16.0.0 (clang-1600.0.26.6) ; arm64-Darwin`. Binaries were built from the repo
root with `--strict --sanitize=none` for `.cp` demos (required so the unguarded
one-over is a genuine raw copy, not a sanitizer trap). The absolute
`examples/screenshots/captures/tmp/...` paths in the commands below are the
machine-local output paths recorded by the capture step.

| SVG | Source transcript | Safety property shown |
| --- | --- | --- |
| `unsafe_vs_safe_memcpy.svg` | `examples/screenshots/captures/unsafe_vs_safe_memcpy.txt` | Checked `cplus_memcpy_s` refuses a one-over (`rc=406`) and leaves the canary intact, while a forked raw `memcpy` corrupts its own canary; exact fit still succeeds. |
| `bounds_query.svg` | `examples/screenshots/captures/bounds_query.txt` | `stdlib_mem_span_ok` / `stdlib_mem_query` refuse a one-over span and a stale-generation key (bounds + lifecycle categories); interior-pointer containment is checked. |
| `lifetime_refusal.svg` | `examples/screenshots/captures/lifetime_refusal.txt` | Guarded write/read refuse a stale borrow token from metadata only (destination untouched), and a second owner release is refused with `double_free_cat=1`. Includes the re-run `bounds_demo` lifecycle-category refusal line. |
| `sync_double_destroy.svg` | `examples/screenshots/captures/sync_double_destroy.txt` | `cplus_mtx_destroy_checked`, `cplus_cnd_destroy_checked` and `cplus_thrd_join_checked` return `EINVAL` (22) on a second destroy/join and on NULL / all-zero handles. |

## `unsafe_vs_safe_memcpy.svg`

- Source: `examples/screenshots/captures/unsafe_vs_safe_memcpy.txt` (exit code 0)
- Build command:

  ```sh
  ./build/c+ build examples/expanded/safety/memcpy_s_demo.cp --strict --sanitize=none -I. -o "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/memcpy_s_demo" -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
  ```

- Run command: `/usr/bin/timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/memcpy_s_demo`
- Safety property: before/after contrast — a forked child performs the raw
  `__builtin_memcpy(..., 9)` into an 8-byte field and corrupts its canary
  (`0xa5` -> `0x2a`, `child_exit=9`), while the same one-over through
  `cplus_memcpy_s(dst, 8, src, 9)` is refused with safeclib `ESNOSPC = 406`,
  leaving the demo-process canary at `0xa5`. The exact-fit call returns `0` and
  copies all 8 bytes (positive control). The `IGNORE CONSTRAINT HANDLER:` line
  is genuine output from safeclib's default handler and is preserved verbatim.
- Expected tokens verified in SVG: `memcpy_s checked_one_over REFUSED rc=406 canary=0xa5 intact=1`, `IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max`, `memcpy_s parent_canary OK canary=0xa5`.

## `bounds_query.svg`

- Source: `examples/screenshots/captures/bounds_query.txt` (exit code 0)
- Build command:

  ```sh
  ./build/c+ build examples/expanded/safety/bounds_demo.cp --strict --sanitize=none -I. -o "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/bounds_demo"
  ```

- Run command: `/usr/bin/timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/bounds_demo`
- Safety property: exact-fit `[0,16)` and interior `[4,16)` spans pass;
  one-over `[0,17)` is refused by the pure predicate with `query=-1` and
  `bounds_cat=1`; an interior address has no metadata of its own but is
  contained by `stdlib_mem_span_contains(base, size, p+8, 24)`; a
  `stdlib_mem_transfer` generation bump makes the captured key stale so
  `stdlib_mem_query` refuses with `lifecycle_cat=1` while a fresh key succeeds.
- Expected tokens verified in SVG: `bounds one_over REFUSED span_1over=0 span_tail=0 query=-1 bounds_cat=1`, `bounds stale_generation REFUSED moved=1 old_key_current=0 old_query=-1 lifecycle_cat=1 fresh_query=1`.

## `lifetime_refusal.svg`

- Source: `examples/screenshots/captures/lifetime_refusal.txt` (exit code 0)
- Build command:

  ```sh
  ./build/c+ build examples/expanded/safety/lifetime_demo.cp --strict --sanitize=none -I. -o "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/lifetime_demo"
  ```

- Run command: `/usr/bin/timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/lifetime_demo`
- Additional real output in the same transcript (lifecycle-category line):
  built with `./build/c+ build examples/expanded/safety/bounds_demo.cp --strict --sanitize=none -I. -o " .../tmp/bounds_demo"` and run with `/usr/bin/timeout 30 .../tmp/bounds_demo`.
- Safety property: a valid borrow round-trips `{11,22,33,44}`; after
  `lifetime_owner_release`, `lifetime_read` / `lifetime_write` refuse from
  metadata only (`CPLUS_EXIT_USE_AFTER_FREE`) with the `0x99` destination
  sentinel unchanged (`untouched=1`); a second `lifetime_owner_release`
  returns `STDLIB_FAIL` with `CPLUS_EXIT_DOUBLE_FREE` (`double_free_cat=1`)
  and the live-block count is unchanged.
- Expected tokens verified in SVG: `lifetime stale_token_refused REFUSED released=1 retired=1 not_live=1 read=-1 cat=1 untouched=1 write=-1 wcat=1`, `lifetime double_release_refused REFUSED init=1 first=1 second=-1 double_free_cat=1 live_stable=1`.

## `sync_double_destroy.svg`

- Source: `examples/screenshots/captures/sync_double_destroy.txt` (exit code 0)
- Build command:

  ```sh
  /usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -I. -Ilibc/install-include examples/expanded/safety/sync_demo.c build/libc/libcplus_hardened.a -pthread -o "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/sync_demo"
  ```

- Run command: `/usr/bin/timeout 30 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/examples/screenshots/captures/tmp/sync_demo`
- Safety property: valid init/destroy returns 0; a second
  `cplus_mtx_destroy_checked` / `cplus_cnd_destroy_checked` returns `EINVAL`
  (22) without touching pthread again; the first `cplus_thrd_join_checked`
  returns 0 and propagates result 42 while the second returns `EINVAL`; NULL
  mutex/condition and an all-zero thread handle are refused with `EINVAL`
  without being dereferenced.
- Expected tokens verified in SVG: `sync mtx_destroy REFUSED init_destroy=0 double=22 (EINVAL=22)`, `sync cnd_destroy REFUSED init_destroy=0 double=22 (EINVAL=22)`, `sync thrd_join REFUSED join=0 result=42 double=22 (EINVAL=22)`, `sync null_handles REFUSED mtx_NULL=22 cnd_NULL=22 thrd_zero=22 (EINVAL=22)`.

## Verification performed

- `rm -f *.svg && ./render.sh` -> exit `0`, four SVGs regenerated.
- `xmllint --noout *.svg` -> all four are well-formed XML.
- `grep -qF` for each full verbatim refusal line above -> all found in the
  corresponding SVG (see per-image "Expected tokens" lists).
- No transcript metadata (`CAPTURED_AT_UTC`, `TOKEN_OK`, `VERDICT:`, BEGIN/END
  markers) is present in any SVG.

/* C23 <uchar.h> UTF-8 conversions (mbrtoc8 / c8rtomb): BLOCKED, no body shipped.
 *
 * STATUS: BLOCKED -- there is no pinned musl source for these two functions.
 *   - pinned musl commit 7813e9119eb5fe681b6750b8a033a0490bca0941: mbrtoc8.c /
 *     c8rtomb.c return HTTP 404 (absent); the vendored tree at that commit has
 *     no src/multibyte/ directory at all.
 *   - tag v1.2.6: both paths 404 (absent).
 *   - branch master (checked 2026-11-22): src/multibyte/ lists only the C11
 *     UTF-16/UTF-32 files; commit search for "mbrtoc8" returns zero commits.
 *   The only musl-affiliated source is an UNMERGED 2023 mailing-list patch by
 *   Jens Gustedt (MIT), whose exact message-ids, URLs, blob ids and verbatim
 *   text are recorded in docs/libc/UCHARC23_UPSTREAM.md.
 *
 * No conversion algorithm was written from scratch and no symbol is emitted
 * here. Copying the unmerged patch would require host-adaptation edits (musl
 * mbstate_t/state-encoding assumptions, weak_alias, _Alignas, NULL-src reset,
 * a C-locale defect flagged upstream) far beyond the sanctioned "rename +
 * include + log" integration edits, so it would be incorrect on this host and
 * would violate the no-fabrication rule. See the upstream document for the full
 * evidence and for the cplus_mbrtoc8 / cplus_c8rtomb declarations the
 * orchestrator will add to cplus_uchar.h.
 *
 * This translation unit deliberately contains no externally visible code; the
 * typedef below only keeps it a well-formed ISO C translation unit so it is
 * safe to add to a source list. tests/libc/test_uchar_c23.c prints the exact
 * blocker and SKIPs when no backend is present.
 */
typedef int cplus_uchar_c23_blocker_marker;

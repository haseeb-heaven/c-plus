# Documented gaps (not run as passing tests)

These are real C bugs that safe C+ does **not** currently detect automatically.
They are kept here as evidence of the limitation, not as verified safety tests.

- `raw_uaf.c` — raw `malloc`/`free` followed by a stale dereference. ASan is
  broken on this host, so C+ cannot intercept the dereference. Use
  `cplus_alloc` + `cplus_check_live()` (see `classic_bug2_uaf`) for an explicit
  dangling-pointer check.
- `missing_terminator.c` — `strncpy` into a buffer with no room for `'\0'`.
  C+ does not check string termination; reserve `strlen + 1` bytes as in
  `classic_bug8_terminator`.
- `macro_double_eval.c` — `SQUARE(i++)` expands to `((i++) * (i++))`. C+
  does not yet diagnose repeated macro-parameter evaluation; use an inline
  function instead.

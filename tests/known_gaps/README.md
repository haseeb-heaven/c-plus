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
- `abi_mismatch/file1.c` + `file2.c` — cross-translation-unit declaration
  mismatch (`int` vs `double`) links cleanly in C. C+ compiles one TU at a
  time and has no whole-program ABI check, so this remains undetected.
- `heuristics/classic_bug10_reassign_leak.c` — pointer reassignment leak cannot
  be proven reliably with token tracking (false positives on real C such as
  `c4.c`); the check was removed.
- `heuristics/classic_bug51_bitfield.c` — bit-field layout is
  implementation-defined but valid C; rejecting it was unsound and removed.
- `heuristics/classic_bug20_signconvert.c` — signed/unsigned comparison needs
  real type information; the token heuristic produced false positives on valid
  C and was removed. Clang's `-Wsign-compare` remains as a warning.
- `heuristics/classic_bug13_literal_write.c` — writing through a `char *`
  initialized from a string literal is UB, but promoting Clang's qualifier
  warning to an error breaks valid legacy C (e.g. `c4.c`). It is now a warning.
- `heuristics/classic_bug34_longjmp.c` — `setjmp`/`longjmp` are valid C but
  bypass deterministic cleanup. They now produce a warning, not an error, so
  real C programs (e.g. Lua) still compile; RAII is not guaranteed across
  non-local jumps.

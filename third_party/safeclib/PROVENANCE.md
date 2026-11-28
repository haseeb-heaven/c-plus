# safeclib provenance

- Upstream: https://github.com/rurban/safeclib
- Commit: 90b076e94c4b0fb202dddf948d5d620e5395c1b0
- License: MIT (see COPYING; Cisco 2012-2013, Reini Urban 2017)
- Vendored by: Agent 2 (PR #17, agent/modern-stdlib)
- Scope: full `src/` and `include/` trees copied for evaluation/integration; C+ build selects dependency-closed subsets. Original files preserved; local adaptations documented here and in git history.

## Local adaptations

- 2026-11-21: vendored upstream source tree; added PROVENANCE/UPSTREAM_COMMIT.
- 2026-11-21: C+ build generates minimal `config.h` and compiles selected safeclib sources with `-D__STDC_WANT_LIB_EXT1__=0` plus per-symbol `-D...=cplus_sc_*` prefixes to isolate from host libc; a `cplus_sc_*` adapter avoids collisions. (An earlier note named `-DCPLUS_SAFECLIB_BUILD`; that flag is not passed by the current `scripts/build_safecrt.sh`.)
- 2026-11-22: Fixed trailing-token `*ptr = dest` state update and end-of-tokens (`dest == NULL && *dmaxp == 0 && **ptr == '\0'`) return in `src/str/strtok_s.c`; added a public 4-arg `cplus_sc_strtok_s` adapter in `src/cplus_sc_wrappers.c` that forwards to the 5-arg internal `cplus_sc__strtok_s_chk` with its `BOS(dest)` argument.
- 2026-11-22: SECURITY PATCH in `src/str/strnlen_s.c`: changed
  `while (*str && smax)` to `while (smax && *str)`. Upstream evaluated the
  dereference before the bound, reading one byte past a fully populated
  unterminated source at `smax`. Triggered by an adversarial guard-page test
  (`tests/libc/test_memory_attack.c`); fix verified by UBSan and canaries.
## Local integration: timingsafe comparisons (2026-11-22)

- Integrated `src/extmem/timingsafe_bcmp.c` and `src/extmem/timingsafe_memcmp.c`
  verbatim (no source edits). They are compiled by `scripts/build_safecrt.sh`
  with only `-Dinvoke_safe_mem_constraint_handler=cplus_sc_invoke_safe_mem_constraint_handler`
  so the already-linked C+ constraint handler is used.
- Exported as `cplus_sc_timingsafe_bcmp` / `cplus_sc_timingsafe_memcmp` through
  `stdlib/safe/src/safeclib_ext_wrappers.c` and declared in
  `stdlib/safe/safe_ext_mem.h`. The upstream `_timingsafe_*_chk` symbols are
  deliberately not prefixed with -D: safeclib's BOS_OVR2 attribute macros
  reference the same token and break when it is redefined.
- Tests: `tests/crt/unit/test_timingsafe_s.c`.
- License/MIT and upstream commit unchanged (see top of this file).


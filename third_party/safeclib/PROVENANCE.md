# safeclib provenance

- Upstream: https://github.com/rurban/safeclib
- Commit: 90b076e94c4b0fb202dddf948d5d620e5395c1b0
- License: MIT (see COPYING; Cisco 2012-2013, Reini Urban 2017)
- Vendored by: Agent 2 (PR #17, agent/modern-stdlib)
- Scope: full `src/` and `include/` trees copied for evaluation/integration; C+ build selects dependency-closed subsets. Original files preserved; local adaptations documented here and in git history.

## Local adaptations

- 2026-11-21: vendored upstream source tree; added PROVENANCE/UPSTREAM_COMMIT.
- 2026-11-21: C+ build generates minimal `config.h` and compiles selected safeclib sources with `-DCPLUS_SAFECLIB_BUILD` to isolate from host libc; symbol prefixing/cplus adapter used to avoid collisions.
- 2026-11-22: Fixed trailing-token `*ptr = dest` state update and end-of-tokens (`dest == NULL && *dmaxp == 0 && **ptr == '\0'`) return in `src/str/strtok_s.c`; added 5-arg `cplus_sc_strtok_s` adapter in `src/cplus_sc_wrappers.c` matching 5-arg `strtok_s` call signature.

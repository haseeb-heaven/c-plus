# CTL provenance

- Upstream: https://github.com/rurban/ctl
- Commit: ede5f455392f2380dd16aead26835eece6cd20e4
- License: MIT (see LICENSE; Gustav Louw 2020-2021, Reini Urban 2020-2021)
- Vendored by: Agent 2 (PR #17, agent/modern-stdlib)
- Scope: `ctl/` headers copied verbatim (header-only typed C containers: vector,
  string, deque, list, map, set, ...). LICENSE/README/SECURITY preserved.

## Local adaptations

- 2026-11-22: vendored upstream headers unchanged; added UPSTREAM_COMMIT.
- Consumers must `#include <string.h>/<stdlib.h>` and, for POD element types,
  `#define POD` before the container include (upstream requirement, documented).
- No source edits to `ctl/*.h`. C+ tests live in `tests/ctl/`.

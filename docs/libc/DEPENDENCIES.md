# Candidate dependency evaluation (Agent 2)

Each candidate was evaluated before adoption: license, maintenance posture,
portability, allocation behaviour, API compatibility, and test coverage.
Adopted components are pinned and vendored with attribution; rejected/deferred
candidates are recorded with the reason. No candidate is adopted automatically.

| Candidate | License | Decision | Notes |
|---|---|---|---|
| rurban/safeclib `90b076e9…` | MIT | **Adopted (primary checked backend)** | Vendored `third_party/safeclib/`; dependency-closed mem/str subset compiled with `-D` symbol prefixing (`cplus_sc_*`); C+ adapters `cplus_*`. |
| intel/safestringlib `f4491915…` | MIT | **Vendored (reference)** | `third_party/intel-safestringlib/`. Overlapping mem/str runtime no longer linked; retained for provenance/parity checks until full removal is verified. |
| guillermocalvo/exceptions4c `9f787958…` | Apache-2.0 | **Adopted** | Unified exception engine (TRY/CATCH/FINALLY/THROW + thread-local contexts). `third_party/exceptions4c/`; glue in `stdlib/safe/safe_except.{h,c}`. Tests `tests/exceptions/`. |
| rurban/ctl `ede5f455…` | MIT | **Adopted** | `third_party/ctl/` header-only typed C containers (vector/string/deque/list/map/set). Tests in `tests/ctl/`. |
| musl `7813e911…` | MIT | **Vendored (reference)** | `libc/upstream/musl/` — selected standard implementations used as reference/provenance for the C+ libc slice. |
| llvm/llvm-project (libc) | Apache-2.0 with LLVM exception | **Deferred** | Large, CMake-heavy; not dependency-closed for this tree. Revisit for math/printf parity with a pinned subset. |
| ibireme/yyjson | MIT | **Vendored (existing)** | `third_party/yyjson/` already present; JSON parse/serialize candidate. Not yet wrapped in a C+ checked API. |
| clog | MIT | **Vendored (existing)** | `third_party/clog/` logging library already present. |
| OpenSSL | Apache-2.0 | **Deferred** | Heavy dependency, separate security-update cadence; not embedded. Prefer system library via documented optional integration. |
| libuv | MIT | **Deferred** | Pulls OS-level async I/O + threads; needs cross-platform CI + threading contracts before adoption. |
| Apple libc / MS UCRT / glibc | platform | **Not vendored** | Used only through installed system headers/libs. No proprietary CRT source copied. |

## Adoption rules applied
- Pin exact upstream revision; preserve LICENSE/COPYING/attribution.
- Vendor dependency-closed subsets; avoid symbol collisions with the host CRT
  (build-time `-D` prefixing; no host libc replacement).
- Every adopted API gets positive + negative tests before being called complete.
- Do not claim a library layer (even checked `_s` APIs) provides Rust-grade
  memory safety; ownership/lifetime/provenance remain compiler/Agent-1 concerns.

## Current test evidence (local, this host)
- `sh tests/crt/unit/run_unit_tests.sh` → **73 passed, 0 failed**
- `sh tests/crt/run_all_crt.sh` → **ALL CRT CHECKS PASSED** (corpus 10/0, safecrt 6/0, examples 26/0, adversarial 4/0, fuzz 4/0)
- `sh tests/libc/run_libc_tests.sh` → **25 passed, 0 failed**
- `sh tests/libc/install_smoke.sh` → **ok**
- `sh tests/ctl/run_ctl_tests.sh` → **3 passed, 0 failed**

Remote Linux/Windows CI and sanitizer runs are configured in
`.github/workflows/libc-crt.yml` but are **not independently verified** from this
host.

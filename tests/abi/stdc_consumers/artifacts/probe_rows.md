| `safe_headers_only` | `safeclib-style overlay headers alone (no cplus_libc.h)` | pass | 0 | `tests/abi/stdc_consumers/artifacts/safe_headers_only.cpp.log` | OK |
| `cplus_libc_only` | `cplus_libc.h alone — root-cause isolation (expected FAIL)` | fail | 0 | `tests/abi/stdc_consumers/artifacts/cplus_libc_only.cpp.log` | UNEXPECTED |

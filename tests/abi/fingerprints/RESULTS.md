# ABI fingerprint tool — exact commands and results

Evidence collected: **2026-11-23T04:00:38Z** (`date -u`) on the host toolchain
below. All commands were run from the repository root
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch
`agent/modern-stdlib`). No Python was used at any point: capture, JSON
conversion and comparison are shell + C only. No compiler, runtime, stdlib
header, or existing test file was modified.

## 1. Toolchain / environment in the baseline

| item | value |
| --- | --- |
| compiler | `/usr/bin/clang` — `Apple clang version 16.0.0 (clang-1600.0.26.6)` |
| target triple (from `clang -print-target-triple`) | `arm64-apple-darwin27.2.0` |
| symbol tool | `/usr/bin/nm` (`-gU`) |
| diff tool | `/usr/bin/diff` (`-u`) |
| archives scanned | `build/libc/libcplus_hardened.a`, `build/safe/libcplus_safecrt.a` |
| exported symbols found | libc 782, safecrt 244 (defined external symbols, sorted, unique) |

## 2. Files produced

| file | sha256 |
| --- | --- |
| `tests/abi/fingerprints/baseline.txt` | `5efa8a7d14e7665542025a2b07a7a2d3fc6c3c7812a3a11f3d926787862d7c1b` |
| `tests/abi/fingerprints/baseline.json` | `37689878681cc6b05a2bf9e16e558d8c087b294bd08ef6d651e489e6150c2f88` |

Counts: `baseline.txt` 1163 lines / 69038 bytes (3 comment lines + 1160
records); `baseline.json` 1 line / 71216 bytes.

Records by section:
`V 4` (metadata), `L 89` (layout), `C 27` (compiler defines), `S 1028`
(symbols), `F 12` (function signatures) — total 1160.

Captured values agree with the documented ABI table in
`tests/abi/ABI_COMPAT_NOTES.md` / `tests/abi/abi_layouts.c`, e.g.
`cplus_alloc_meta` size 40 / align 8 with offsets 0, 8, 16, 24, 32;
`cplus_thrd_t` 32/8; `cplus_mtx_t` 128/8; `cplus_cnd_t` 128/8;
`cplus_once_flag` 64/8; `char16_t` 2/2; `char32_t` 4/4; `mbstate_t` 128/8;
`cplus_safe_stat` 48/8; `cplus_safe_platform_info` 28/4;
`cplus_safe_log_level` 4/4; `cplus_safe_constraint_mode` 4/4;
`cplus_runtime_handler` 8/8; `errno_t` 4/4; `rsize_t` 8/8.

## 3. Exact commands and exit codes

Every command below exited **0** unless the expected exit code is stated.

```sh
# --- prerequisites already present in the checkout (no rebuild needed) -------
/usr/bin/clang --version
/usr/bin/clang -print-target-triple

# --- capture into the working directory (generates C source, compiles, runs) --
sh tools/abi_compare/run.sh capture
# output:
#   abi_fp: generating C sources in tools/abi_compare/.out
#   abi_fp: compile probe   : /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> tools/abi_compare/.out/abi_fp_probe.c
#   abi_fp: compile json    : /usr/bin/clang -O2 -std=c11 -Wall -Wextra tools/abi_compare/.out/fp_to_json.c
#   abi_fp: wrote tools/abi_compare/.out/fingerprint.txt (1160 records) and fingerprint.json
#   abi_fp: capture complete. baseline candidates:
#     tools/abi_compare/.out/fingerprint.txt
#     tools/abi_compare/.out/fingerprint.json

# --- fully expanded probe compile command (as executed by run.sh) -----------
/usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror \
  -Ilibc/install-include -Ilibc/include -Ilibc -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tools/abi_compare/.out/abi_fp_probe.c -o tools/abi_compare/.out/abi_fp_probe
/usr/bin/clang -O2 -std=c11 -Wall -Wextra \
  tools/abi_compare/.out/fp_to_json.c -o tools/abi_compare/.out/fp_to_json
tools/abi_compare/.out/abi_fp_probe                       # prints L + F records
/usr/bin/clang -dM -E -x c /dev/null | awk '<abi-define regex>' | sort -u
/usr/bin/nm -gU build/libc/libcplus_hardened.a | awk '$2 ~ /^[A-Z]$/ && NF>=3 {print $3}' | sort -u
/usr/bin/nm -gU build/safe/libcplus_safecrt.a | awk '$2 ~ /^[A-Z]$/ && NF>=3 {print $3}' | sort -u
tools/abi_compare/.out/fp_to_json < tools/abi_compare/.out/fingerprint.txt \
  > tools/abi_compare/.out/fingerprint.json

# --- validate the derived JSON (jq is only a validator, not the mechanism) ---
jq -e '(.format == "abi_fp_1")
       and (.sections.L | has("size cplus_alloc_meta"))
       and (.sections.F | has("signature cplus_malloc"))
       and (.sections.V.target_triple != null)' \
  tools/abi_compare/.out/fingerprint.json
# output: true

# --- regenerate the committed baseline FROM AN ACTUAL RUN --------------------
sh tools/abi_compare/run.sh baseline
# output (abridged):
#   abi_fp: generating C sources in /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/abi/fingerprints
#   abi_fp: wrote .../tests/abi/fingerprints/fingerprint.txt (1160 records) and fingerprint.json
#   abi_fp: refreshed committed baseline:
#     .../tests/abi/fingerprints/baseline.txt
#     .../tests/abi/fingerprints/baseline.json

# --- record provenance -------------------------------------------------------
date -u +%Y-%m-%dT%H:%M:%SZ                    # 2026-11-23T04:00:38Z
shasum -a 256 tests/abi/fingerprints/baseline.txt tests/abi/fingerprints/baseline.json
wc -l -c tests/abi/fingerprints/baseline.txt tests/abi/fingerprints/baseline.json
jq -e '.format=="abi_fp_1" and (.sections|has("L")) and (.sections|has("F")) \
       and (.sections|has("C")) and (.sections|has("S"))' \
  tests/abi/fingerprints/baseline.json         # true

# --- compare: clean run vs committed baseline --------------------------------
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt
# output (abridged):
#   abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1160 records) and fingerprint.json
#   abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
#   abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
# exit=0
```

## 4. Negative tests (compare must fail on unexpected differences)

**Test A — tampered layout constant.** Baseline copy with
`L size cplus_alloc_meta 40` changed to `41`:

```sh
sed 's/^L\tsize cplus_alloc_meta\t40$/L\tsize cplus_alloc_meta\t41/' \
  tests/abi/fingerprints/baseline.txt \
  > tools/abi_compare/.out/mismatch_baseline.txt
sh tools/abi_compare/run.sh compare tools/abi_compare/.out/mismatch_baseline.txt
# abi_fp: ABI fingerprint MISMATCH against ...mismatch_baseline.txt.
# --- baseline.filtered
# +++ current.filtered
# -L	size cplus_alloc_meta	41
# +L	size cplus_alloc_meta	40
# exit=1   (expected)
```

**Test B — toolchain drift suppressed by allow-list.** Baseline copy with
`V clang_version` and `C define __clang_major__` altered, compared with the
committed `ignore.example`:

```sh
sed -e 's/^V\tclang_version\t.*/V\tclang_version\tCHANGED/' \
    -e 's/^C\tdefine __clang_major__\t.*/C\tdefine __clang_major__\t99/' \
    tests/abi/fingerprints/baseline.txt \
    > tools/abi_compare/.out/drift_baseline.txt
sh tools/abi_compare/run.sh compare tools/abi_compare/.out/drift_baseline.txt \
    tests/abi/fingerprints/ignore.example
# abi_fp: ABI fingerprint MATCHES baseline (...drift_baseline.txt).
# exit=0   (expected: both changed records are allow-listed)
```

**Test C — same drift WITHOUT the allow-list (proves the ignore is doing work):**

```sh
sh tools/abi_compare/run.sh compare tools/abi_compare/.out/drift_baseline.txt
# abi_fp: ABI fingerprint MISMATCH against ...drift_baseline.txt.
# -V	clang_version	CHANGED
# +V	clang_version	Apple clang version 16.0.0 (clang-1600.0.26.6)
# -C	define __clang_major__	99
# +C	define __clang_major__	16
# exit=1   (expected)
```

## 5. Result summary

| check | result |
| --- | --- |
| probe + JSON helper compile cleanly with `-Wall -Wextra -Werror` (`-Werror` on probe) | pass |
| captured layout values match documented ABI table | pass |
| JSON artifact parses and has all five sections | pass |
| fresh capture vs committed baseline | match (exit 0) |
| tampered layout constant | mismatch (exit 1) — detected |
| toolchain drift with allow-list | match (exit 0) |
| toolchain drift without allow-list | mismatch (exit 1) — detected |

The committed baseline is `tests/abi/fingerprints/baseline.txt` (and its JSON
rendering `baseline.json`), generated from the actual capture in section 3.

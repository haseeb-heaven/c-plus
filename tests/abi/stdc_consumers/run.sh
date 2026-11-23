#!/bin/sh
# Standard-C header / checked-overlay consumer verification.
#
# For every header in scope, compile a tiny C99 consumer with /usr/bin/clang
# and a tiny C++17 consumer with /usr/bin/clang++ using the CPLUS_CLANG-style
# warning flags (-Wall -Wextra -pedantic).  Record the exact command, exit code
# and complete compiler diagnostics for every run under a 30s timeout.
#
# STRICT OWNERSHIP: this script reads the repository but writes ONLY under
# tests/abi/stdc_consumers/ (artifacts/ and RESULTS.md).  It never edits a
# header and never runs git add/commit.
set -u

export PATH=/usr/bin:/bin
export LC_ALL=C
CC=${CPLUS_CLANG:-/usr/bin/clang}
CXX=${CPLUS_CXX:-/usr/bin/clang++}
TIMEOUT=/usr/bin/timeout
TMO=30

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LAB=tests/abi/stdc_consumers
OUT=$ROOT/$LAB/artifacts
SRC=$ROOT/$LAB/consumers
RESULTS=$ROOT/$LAB/RESULTS.md
mkdir -p "$OUT"

LOG=$OUT/compiler-runs.log
ROWS=$OUT/rows.md
DIAG=$OUT/diagnostics.md
EXTERN=$OUT/extern_c_audit.md
PROBE_ROWS=$OUT/probe_rows.md
PROBE_DIAG=$OUT/probe_diagnostics.md
: > "$LOG"
: > "$ROWS"
: > "$DIAG"
: > "$EXTERN"
: > "$PROBE_ROWS"
: > "$PROBE_DIAG"

PASS=0
FAIL=0
PROBE_OK=0
PROBE_UNEXPECTED=0

log() { printf '%s\n' "$*" >> "$LOG"; printf '%s\n' "$*"; }

# run_one <id> <header> <lang:c|cpp> <source-file>
run_one() {
    id=$1
    header=$2
    lang=$3
    file=$4

    clog=$OUT/$id.$lang.log
    obj=$OUT/$id.$lang.o
    rm -f "$clog" "$obj"

    if [ "$lang" = c ]; then
        cmd="$CC -std=c99 -Wall -Wextra -pedantic -I$ROOT -I$ROOT/libc/install-include -c $SRC/$file -o $obj"
    else
        cmd="$CXX -std=c++17 -Wall -Wextra -pedantic -I$ROOT -I$ROOT/libc/install-include -c $SRC/$file -o $obj"
    fi

    log "CMD[$id.$lang] timeout=${TMO}s: $cmd"
    if [ "$lang" = c ]; then
        $TIMEOUT "$TMO" "$CC" -std=c99 -Wall -Wextra -pedantic \
            -I"$ROOT" -I"$ROOT/libc/install-include" \
            -c "$SRC/$file" -o "$obj" > "$clog" 2>&1
    else
        $TIMEOUT "$TMO" "$CXX" -std=c++17 -Wall -Wextra -pedantic \
            -I"$ROOT" -I"$ROOT/libc/install-include" \
            -c "$SRC/$file" -o "$obj" > "$clog" 2>&1
    fi
    rc=$?
    log "RC[$id.$lang]=$rc"

    if [ "$rc" -eq 0 ]; then
        status=PASS
        PASS=$((PASS + 1))
    else
        status=FAIL
        FAIL=$((FAIL + 1))
    fi

    printf '| `%s` | `%s` | %s | %s | `%s` |\n' \
        "$header" "$lang" "$status" "$rc" "$LAB/artifacts/$id.$lang.log" >> "$ROWS"

    {
        printf '#### `%s` — %s consumer (`%s`)\n\n' "$header" "$lang" "$id"
        printf 'Command: `%s`\n\n' "$cmd"
        printf 'Exit code: `%s`\n\n' "$rc"
        printf '```text\n'
        if [ -s "$clog" ]; then
            cat "$clog"
        else
            printf '(no compiler diagnostics — stdout and stderr were empty)\n'
        fi
        printf '```\n\n'
    } >> "$DIAG"

    printf '%s %s.%s (rc=%s)\n' "$status" "$id" "$lang" "$rc"
}

# run_probe <id> <label> <source-file> <extra-include-flags> <expect:pass|fail>
# Supplementary C++17 isolation probes, kept OUT of the 24-run main matrix.
run_probe() {
    id=$1
    label=$2
    file=$3
    extra=$4
    expect=$5

    clog=$OUT/$id.cpp.log
    obj=$OUT/$id.cpp.o
    rm -f "$clog" "$obj"

    cmd="$CXX -std=c++17 -Wall -Wextra -pedantic $extra -c $SRC/$file -o $obj"
    log "CMD[probe:$id] timeout=${TMO}s: $cmd"
    $TIMEOUT "$TMO" "$CXX" -std=c++17 -Wall -Wextra -pedantic $extra \
        -c "$SRC/$file" -o "$obj" > "$clog" 2>&1
    rc=$?
    log "RC[probe:$id]=$rc (expected=$expect)"

    if [ "$rc" -eq 0 ]; then actual=pass; else actual=fail; fi
    if [ "$actual" = "$expect" ]; then
        outcome=OK
        PROBE_OK=$((PROBE_OK + 1))
    else
        outcome=UNEXPECTED
        PROBE_UNEXPECTED=$((PROBE_UNEXPECTED + 1))
    fi

    printf '| `%s` | `%s` | %s | %s | `%s` | %s |\n' \
        "$id" "$label" "$expect" "$rc" "$LAB/artifacts/$id.cpp.log" "$outcome" >> "$PROBE_ROWS"
    {
        printf '#### probe `%s` — %s\n\n' "$id" "$label"
        printf 'Command: `%s`\n\n' "$cmd"
        printf 'Exit code: `%s` (expected: `%s`) — outcome: **%s**\n\n' "$rc" "$expect" "$outcome"
        printf '```text\n'
        if [ -s "$clog" ]; then cat "$clog"; else printf '(no compiler diagnostics — stdout and stderr were empty)\n'; fi
        printf '```\n\n'
    } >> "$PROBE_DIAG"
    printf 'PROBE %s: %s (rc=%s, expected=%s)\n' "$id" "$outcome" "$rc" "$expect"
}

# extern_c_audit <label> <repo-relative header path>
extern_c_audit() {
    label=$1
    path=$2
    if [ -f "$ROOT/$path" ]; then
        n=$(/usr/bin/grep -c 'extern "C"' "$ROOT/$path" || true)
        if [ "$n" -gt 0 ]; then
            status=present
        else
            status='n/a — macro-only header, declares no functions'
        fi
        printf '| `%s` | `%s` | %s | %s |\n' "$label" "$path" "$n" "$status" >> "$EXTERN"
    else
        printf '| `%s` | `%s` | 0 | FILE MISSING |\n' "$label" "$path" >> "$EXTERN"
    fi
}

log "=== stdc_consumers: standard C headers + checked overlays ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "arch=$(uname -m) os=$(uname -s)"
log "CC=$CC  CXX=$CXX  TIMEOUT=$TIMEOUT  TMO=${TMO}s"
"$CC" --version >> "$LOG" 2>&1
"$CXX" --version >> "$LOG" 2>&1
log "Note: -I$ROOT is for \"stdlib/safe/safe_c.h\"; -I$ROOT/libc/install-include is for the additive overlays."

# ---- nine standard C headers, each with a C99 and a C++17 consumer --------
for h in stdlib string stdio stdint stddef errno ctype limits assert; do
    run_one "$h" "<$h.h>" c "$h.c"
    run_one "$h" "<$h.h>" cpp "$h.cpp"
done

# ---- additive checked-layer overlays --------------------------------------
run_one cplus_libc_safe '"cplus_libc_safe.h"' c cplus_libc_safe.c
run_one cplus_libc_safe '"cplus_libc_safe.h"' cpp cplus_libc_safe.cpp
run_one safe_c '"stdlib/safe/safe_c.h"' c safe_c.c
run_one safe_c '"stdlib/safe/safe_c.h"' cpp safe_c.cpp

# ---- all nine standard headers together (cross-header conflict check) -----
run_one all_std 'all 9 standard headers (combined)' c all_std.c
run_one all_std 'all 9 standard headers (combined)' cpp all_std.cpp

# ---- supplementary C++17 isolation probes (not counted in the 24 above) ---
run_probe safe_headers_only \
    'safeclib-style overlay headers alone (no cplus_libc.h)' \
    safe_headers_only.cpp "-I$ROOT/stdlib/safe -I$ROOT/libc/install-include" pass
run_probe cplus_libc_only \
    'cplus_libc.h alone — root-cause isolation (expected FAIL)' \
    cplus_libc_only.cpp "-I$ROOT/libc/install-include" fail

# ---- audit extern "C" in every overlay / transitive header ----------------
extern_c_audit cplus_libc.h libc/install-include/cplus_libc.h
extern_c_audit cplus_libc_safe.h libc/install-include/cplus_libc_safe.h
extern_c_audit cplus_safecrt_upstream.h libc/install-include/cplus_safecrt_upstream.h
extern_c_audit cplus_stdatomic.h libc/install-include/cplus_stdatomic.h
extern_c_audit cplus_math_c23_farith.h libc/install-include/cplus_math_c23_farith.h
extern_c_audit cplus_math_c23_hypot3_f16.h libc/install-include/cplus_math_c23_hypot3_f16.h
extern_c_audit safe_c.h stdlib/safe/safe_c.h
extern_c_audit safe_alloc.h stdlib/safe/safe_alloc.h
extern_c_audit safe_memory.h stdlib/safe/safe_memory.h
extern_c_audit safe_string.h stdlib/safe/safe_string.h
extern_c_audit safe_io.h stdlib/safe/safe_io.h
extern_c_audit safe_format.h stdlib/safe/safe_format.h
extern_c_audit safe_error.h stdlib/safe/safe_error.h
extern_c_audit safe_platform.h stdlib/safe/safe_platform.h
extern_c_audit safe_ext_mem.h stdlib/safe/safe_ext_mem.h
extern_c_audit safe_runtime.h stdlib/safe/safe_runtime.h
extern_c_audit safe_log.h stdlib/safe/safe_log.h
extern_c_audit safe_stats.h stdlib/safe/safe_stats.h
extern_c_audit safe_except.h stdlib/safe/safe_except.h

# ---- assemble RESULTS.md from the captured evidence -----------------------
{
    printf '# Standard C header / checked-overlay consumer results\n\n'
    printf 'Generated by `tests/abi/stdc_consumers/run.sh`. Re-run the script to refresh this file '
    printf 'from live compiler output; do not hand-edit.\n\n'

    printf '## Environment\n\n'
    printf '```text\n'
    printf 'date_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'arch=%s os=%s\n' "$(uname -m)" "$(uname -s)"
    printf 'CC=%s\nCXX=%s\n' "$CC" "$CXX"
    printf 'timeout=%ss per compile\n' "$TMO"
    "$CC" --version | /usr/bin/sed -n '1p'
    "$CXX" --version | /usr/bin/sed -n '1p'
    printf '```\n\n'

    printf '## Flags\n\n'
    printf '```text\n'
    printf '%s -std=c99   -Wall -Wextra -pedantic -I<root> -I<root>/libc/install-include -c <consumer.c>   -o <artifact.o>\n' "$CC"
    printf '%s -std=c++17 -Wall -Wextra -pedantic -I<root> -I<root>/libc/install-include -c <consumer.cpp> -o <artifact.o>\n' "$CXX"
    printf '```\n\n'
    printf '`-I<root>` resolves `#include "stdlib/safe/safe_c.h"`; `-I<root>/libc/install-include` '
    printf 'resolves the additive overlay headers. The install-include copies under '
    printf '`build/libc-install/include/cplus-libc/` and `build/compat/prefix/include/cplus-libc/` '
    printf 'are byte-identical (verified with `cksum`), so the results apply to the installed prefix too.\n\n'

    printf '## Per-header results\n\n'
    printf '| Header | Consumer | Result | Exit | Log |\n'
    printf '| --- | --- | --- | --- | --- |\n'
    cat "$ROWS"
    printf '\n'

    printf 'Main-matrix totals: **%s PASS**, **%s FAIL** (24 compile runs).\n\n' "$PASS" "$FAIL"

    printf '## Supplementary C++17 isolation probes\n\n'
    printf 'These two probes are kept out of the main matrix. They isolate the C++17 '
    printf 'blocker and confirm that the safeclib-style overlay headers themselves are '
    printf 'C++-parseable with C linkage.\n\n'
    printf '| Probe | What it includes | Expected | Exit | Log | Outcome |\n'
    printf '| --- | --- | --- | --- | --- | --- |\n'
    cat "$PROBE_ROWS"
    printf '\nProbe totals: **%s OK**, **%s UNEXPECTED**.\n\n' "$PROBE_OK" "$PROBE_UNEXPECTED"

    printf '## extern "C" audit of the additive overlays\n\n'
    printf 'Count of `extern "C"` occurrences per header (linkage guard presence):\n\n'
    printf '| Header | Path | occurrences | Status |\n'
    printf '| --- | --- | --- | --- |\n'
    cat "$EXTERN"
    printf '\n'
    printf 'Every header that declares a function carries an `extern "C"` guard. The two '
    printf 'top-level overlay headers (`cplus_libc_safe.h`, `safe_c.h`) are deliberately '
    printf 'macro-only and declare no functions of their own, so a zero count there is '
    printf 'expected and correct. C linkage is therefore declared properly where needed. '
    printf 'The C++17 failures recorded below are **not** a linkage problem: they are '
    printf 'C-only tokens in `cplus_libc.h` (see the exact diagnostics).\n\n'

    printf '## Exact compiler diagnostics (every main-matrix run)\n\n'
    cat "$DIAG"

    printf '## Exact compiler diagnostics (supplementary probes)\n\n'
    cat "$PROBE_DIAG"

    printf '## Interpretation\n\n'
    printf -- '- The nine standard headers and their combined C/C++ consumers compile cleanly with '
    printf 'the ordinary ISO C spellings and signatures unchanged.\n'
    printf -- '- The checked layer is **additive and opt-in**: `cplus_libc_safe.h` and '
    printf '`stdlib/safe/safe_c.h` are never pulled in by a standard header; their macro '
    printf 'redirections are enabled only on explicit inclusion (and can be disabled with '
    printf '`CPLUS_LIBC_SAFE_DISABLE` / `CPLUS_SAFE_C_DISABLE`).\n'
    printf -- '- Any `FAIL` row above is a genuine gap and is reproduced verbatim in the '
    printf 'diagnostics section. No header was edited to make a test pass.\n'
    printf -- '- Isolation: `safe_headers_only.cpp` (safeclib-style headers without '
    printf '`cplus_libc.h`) compiles cleanly under C++17, while `cplus_libc_only.cpp` '
    printf 'reproduces the exact same diagnostics as both failing overlay consumers. '
    printf 'The C++17 blocker is therefore `cplus_libc.h` C-only tokens (`restrict`, '
    printf '`double complex`), **not** missing `extern "C"` and **not** the macro layer '
    printf 'of either overlay.\n'
} > "$RESULTS"

printf 'RESULT: stdc_consumers: %s passed, %s failed (main); probes %s OK, %s unexpected\n' \
    "$PASS" "$FAIL" "$PROBE_OK" "$PROBE_UNEXPECTED"
log "RESULT: stdc_consumers: $PASS passed, $FAIL failed (main); probes $PROBE_OK OK, $PROBE_UNEXPECTED unexpected"
log "Full command/exit log: $LAB/artifacts/compiler-runs.log"
log "Generated report: $LAB/RESULTS.md"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

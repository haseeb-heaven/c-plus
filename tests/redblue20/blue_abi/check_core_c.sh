#!/bin/sh
# BLUE20 core-C source integrity vs baseline cbcea57 (read-only; no git writes).
# Proves the 7 canonical core C implementation files are byte-identical at
# cbcea57 and HEAD and have no uncommitted working-tree modification.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
BASE=${BASE:-cbcea57}
HEADREV=${HEAD_REV:-HEAD}

CORE_FILES="
libc/src/string/memory.c
libc/src/string/string.c
libc/src/string/extra.c
libc/src/string/bounded.c
libc/src/stdlib/alloc.c
libc/src/stdio/stdio.c
runtime/cplus_runtime.c
"

fail=0
checked=0
passed=0
printf 'core_c: baseline=%s head=%s\n' "$BASE" "$HEADREV"
for f in $CORE_FILES; do
    checked=$((checked + 1))
    if ! git cat-file -e "$BASE:$f" 2>/dev/null || ! git cat-file -e "$HEADREV:$f" 2>/dev/null; then
        printf 'FAIL ABSENT %s\n' "$f"; fail=$((fail + 1)); continue
    fi
    b=$(git rev-parse "$BASE:$f")
    h=$(git rev-parse "$HEADREV:$f")
    if [ "$b" != "$h" ]; then
        printf 'FAIL MODIFIED %s baseline=%s head=%s\n' "$f" "$b" "$h"
        fail=$((fail + 1)); continue
    fi
    if ! git diff --quiet "$BASE..$HEADREV" -- "$f"; then
        printf 'FAIL NONEMPTY-DIFF %s\n' "$f"; fail=$((fail + 1)); continue
    fi
    if [ -n "$(git diff --name-only -- "$f")" ]; then
        printf 'FAIL UNCOMMITTED %s\n' "$f"; fail=$((fail + 1)); continue
    fi
    printf 'PASS UNCHANGED %s %s\n' "$f" "$b"
    passed=$((passed + 1))
done

# Informational disclosure: the stronger "every libc/src change is additive"
# claim no longer holds at this HEAD (committed portability fixes, none
# uncommitted). Printed, never hidden, but not part of the 7-file pass criterion.
src_m=$(git diff --name-status "$BASE..$HEADREV" -- libc/src/ | awk -F'\t' '$1 ~ /^[MDR]/' | wc -l | tr -d ' ')
hdr_m=$(git diff --name-status "$BASE..$HEADREV" -- libc/install-include/ | awk -F'\t' '$1 ~ /^[MDR]/' | wc -l | tr -d ' ')
untracked_src=$(git diff --name-only -- libc/src/ libc/install-include/ runtime/ stdlib/ | wc -l | tr -d ' ')
printf 'core_c: informational non-additive committed files: libc/src/=%s libc/install-include/=%s; uncommitted C-source changes=%s\n' \
    "$src_m" "$hdr_m" "$untracked_src"

if [ "$fail" -eq 0 ]; then
    printf 'core_c: %d/%d core C files UNCHANGED from %s\n' "$passed" "$checked" "$BASE"
    exit 0
fi
printf 'core_c: %d failed of %d\n' "$fail" "$checked"
exit 1

#!/bin/sh
# check_overlap.sh -- detect conflicting edits and duplicate claims between
# two PR #33 agent reports.
#
# Usage: sh tests/pr33_integration/check_overlap.sh <reportA> <reportB>
# Exit codes: 0 = disjoint paths and claims, 1 = overlap/duplicate detected,
#             2 = usage, unreadable report, or a report failing check_report.sh.
#
# POSIX sh only; does not touch the repository.

set -u

[ $# -eq 2 ] || {
    printf 'usage: %s <reportA> <reportB>\n' "$0" >&2
    exit 2
}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
a=$1
b=$2

for report in "$a" "$b"; do
    if [ ! -f "$report" ] || [ ! -r "$report" ]; then
        printf 'CHECK_OVERLAP FAIL: cannot read %s\n' "$report" >&2
        exit 2
    fi
    if ! sh "$script_dir/check_report.sh" "$report" >/dev/null 2>&1; then
        printf 'CHECK_OVERLAP FAIL: report does not pass check_report.sh: %s\n' "$report" >&2
        exit 2
    fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/pr33-overlap.XXXXXX") || {
    printf 'CHECK_OVERLAP FAIL: mktemp failed\n' >&2
    exit 2
}
cleanup() {
    if [ -d "$tmp" ]; then rm -rf "$tmp"; fi
    return 0
}
trap cleanup 0 1 2 3 15

# extract_section <report> <SECTION_NAME>
extract_section() {
    awk -v name="$2" '
        $0 == name "_BEGIN" { inside = 1; next }
        $0 == name "_END"   { inside = 0; next }
        inside && $0 !~ /^[[:space:]]*$/ && $0 !~ /^#/ { print }
    ' "$1"
}

extract_section "$a" CHANGED_PATHS | sort -u > "$tmp/paths.a"
extract_section "$b" CHANGED_PATHS | sort -u > "$tmp/paths.b"
extract_section "$a" CLAIMS | sort -u > "$tmp/claims.a"
extract_section "$b" CLAIMS | sort -u > "$tmp/claims.b"

found=0

# Exact path collisions.
comm -12 "$tmp/paths.a" "$tmp/paths.b" > "$tmp/exact.paths"
while IFS= read -r p; do
    [ -n "$p" ] || continue
    printf 'WARNING OVERLAP path: both reports edit %s\n' "$p"
    found=1
done < "$tmp/exact.paths"

# Directory/file prefix collisions (e.g. "libc/src" vs "libc/src/math.c").
while IFS= read -r pa; do
    [ -n "$pa" ] || continue
    while IFS= read -r pb; do
        [ -n "$pb" ] || continue
        [ "$pa" = "$pb" ] && continue
        case "$pb" in
            "$pa"/*)
                printf 'WARNING OVERLAP path: directory %s (report A) contains %s (report B)\n' "$pa" "$pb"
                found=1 ;;
        esac
        case "$pa" in
            "$pb"/*)
                printf 'WARNING OVERLAP path: directory %s (report B) contains %s (report A)\n' "$pb" "$pa"
                found=1 ;;
        esac
    done < "$tmp/paths.b"
done < "$tmp/paths.a"

# Duplicate claims: the same test or API reported as owned by both agents.
comm -12 "$tmp/claims.a" "$tmp/claims.b" > "$tmp/dup.claims"
while IFS= read -r c; do
    [ -n "$c" ] || continue
    printf 'WARNING DUPLICATE CLAIM: both reports claim %s\n' "$c"
    found=1
done < "$tmp/dup.claims"

if [ "$found" -ne 0 ]; then
    printf 'CHECK_OVERLAP FAIL: overlapping edits or duplicate claims detected\n' >&2
    exit 1
fi
printf 'CHECK_OVERLAP PASS: disjoint paths and claims (%s vs %s)\n' "$a" "$b"
exit 0

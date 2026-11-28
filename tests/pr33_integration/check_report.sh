#!/bin/sh
# check_report.sh -- validate a PR #33 agent report.
#
# Usage: sh tests/pr33_integration/check_report.sh <report-file>
# Exit codes: 0 = report is complete, 1 = required field missing/placeholder,
#             2 = usage / unreadable input.
#
# The schema is specified in docs/abi/PR33_INTEGRATION_GATES.md. POSIX sh only;
# no Python, no network, no repository writes.

set -u

[ $# -eq 1 ] || { printf 'usage: %s <report-file>\n' "$0" >&2; exit 2; }
report=$1
if [ ! -f "$report" ] || [ ! -r "$report" ]; then
    printf 'CHECK_REPORT FAIL: cannot read %s\n' "$report" >&2
    exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/pr33-report.XXXXXX") || {
    printf 'CHECK_REPORT FAIL: mktemp failed\n' >&2
    exit 2
}
cleanup() {
    if [ -d "$tmp" ]; then rm -rf "$tmp"; fi
    return 0
}
trap cleanup 0 1 2 3 15

fail=0
bad() {
    printf 'CHECK_REPORT FAIL: %s\n' "$1" >&2
    fail=$((fail + 1))
}

# Placeholder values are never acceptable evidence. "none" is deliberately
# allowed (it is an explicit, meaningful answer for KNOWN_FAILURES).
is_placeholder() {
    v=$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')
    case "$v" in
        ''|TBD*|TBA*|TODO*|FIXME*|XXX*|PLACEHOLDER*|'N/A'*|NA|'...'|'???'|\
        '<*>'|'*<*>*'|'FILL IN*'|'FILL ME'|'NOT PROVIDED'|'NO OUTPUT'|\
        'SEE BELOW'|'SAME AS ABOVE'|DUMMY*|SAMPLE*)
            return 0 ;;
    esac
    return 1
}

content_lines() {
    awk '!/^[[:space:]]*$/ && !/^#/ { n++ } END { print n + 0 }' "$1"
}

section=
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        ''|'#'*) continue ;;
    esac

    # Inside a named section everything (including KEY: text) is content.
    case "$section" in
        CHANGED_PATHS)
            case "$line" in
                CHANGED_PATHS_END) section= ;;
                *) printf '%s\n' "$line" >> "$tmp/paths" ;;
            esac
            continue ;;
        EXACT_COMMANDS)
            case "$line" in
                EXACT_COMMANDS_END) section= ;;
                *) printf '%s\n' "$line" >> "$tmp/commands" ;;
            esac
            continue ;;
        TEST_OUTPUT)
            case "$line" in
                TEST_OUTPUT_END) section= ;;
                *) printf '%s\n' "$line" >> "$tmp/output" ;;
            esac
            continue ;;
        CLAIMS)
            case "$line" in
                CLAIMS_END) section= ;;
                *) printf '%s\n' "$line" >> "$tmp/claims" ;;
            esac
            continue ;;
    esac

    case "$line" in
        CHANGED_PATHS_BEGIN|EXACT_COMMANDS_BEGIN|TEST_OUTPUT_BEGIN|CLAIMS_BEGIN)
            if [ -n "$section" ]; then bad "nested section at: $line"; fi
            case "$line" in
                CHANGED_PATHS_BEGIN) section=CHANGED_PATHS; : > "$tmp/paths" ;;
                EXACT_COMMANDS_BEGIN) section=EXACT_COMMANDS; : > "$tmp/commands" ;;
                TEST_OUTPUT_BEGIN) section=TEST_OUTPUT; : > "$tmp/output" ;;
                CLAIMS_BEGIN) section=CLAIMS; : > "$tmp/claims" ;;
            esac
            ;;
        CHANGED_PATHS_END|EXACT_COMMANDS_END|TEST_OUTPUT_END|CLAIMS_END)
            bad "section end without begin: $line"
            ;;
        *:*)
            key=${line%%:*}
            val=${line#*:}
            while [ "${val# }" != "$val" ]; do val=${val# }; done
            case "$key" in
                SCOPE|DEPENDENCIES|COMMIT_SHA|TEST_ARTIFACT|KNOWN_FAILURES|INDEPENDENT_REVIEW)
                    f="$tmp/field.$key"
                    if [ -e "$f" ]; then
                        bad "duplicate required field: $key"
                    else
                        printf '%s\n' "$val" > "$f"
                    fi
                    ;;
                *)
                    bad "unknown field: $key"
                    ;;
            esac
            ;;
        *)
            bad "unparsed line: $line"
            ;;
    esac
done < "$report"

if [ -n "$section" ]; then bad "unterminated section: $section"; fi

require_field() {
    key=$1
    f="$tmp/field.$key"
    if [ ! -f "$f" ]; then
        bad "missing required field: $key"
        return 1
    fi
    if [ "$(wc -l < "$f" | tr -d ' ')" -ne 1 ]; then
        bad "field $key must have exactly one value"
        return 1
    fi
    val=$(sed -n '1p' "$f")
    if is_placeholder "$val"; then
        bad "placeholder value in field $key"
        return 1
    fi
    return 0
}

validate_section() {
    name=$1
    file=$2
    if [ ! -f "$file" ]; then return 1; fi
    if [ "$(content_lines "$file")" -lt 1 ]; then
        bad "empty required section: $name"
        return 1
    fi
    while IFS= read -r item; do
        case "$item" in ''|'#'*) continue ;; esac
        if is_placeholder "$item"; then
            bad "placeholder item in $name: $item"
        fi
        if [ "$name" = CHANGED_PATHS ]; then
            case "$item" in
                /*) bad "changed path must be repo-relative: $item" ;;
            esac
        fi
    done < "$file"
    return 0
}

require_field SCOPE
require_field DEPENDENCIES
if require_field COMMIT_SHA; then
    sha=$(sed -n '1p' "$tmp/field.COMMIT_SHA")
    if ! printf '%s\n' "$sha" | grep -Eq '^[0-9a-fA-F]{40}$'; then
        bad "COMMIT_SHA must be 40 hexadecimal characters"
    fi
fi
require_field KNOWN_FAILURES
if require_field INDEPENDENT_REVIEW; then
    review=$(sed -n '1p' "$tmp/field.INDEPENDENT_REVIEW")
    upper=$(printf '%s' "$review" | tr '[:lower:]' '[:upper:]')
    case "$upper" in
        REVIEWED*|'INDEPENDENTLY REVIEWED'*)
            case "$review" in
                *http*|*docs/*|*tests/*|*' PR '*) ;;
                *) bad "INDEPENDENT_REVIEW claims review without linked evidence" ;;
            esac
            ;;
    esac
fi

if ! validate_section CHANGED_PATHS "$tmp/paths"; then
    bad "missing required section: CHANGED_PATHS"
fi
if ! validate_section EXACT_COMMANDS "$tmp/commands"; then
    bad "missing required section: EXACT_COMMANDS"
fi

# TEST_OUTPUT (full output) and TEST_ARTIFACT (path to full output) are
# alternatives; exactly one must be supplied.
if [ -f "$tmp/output" ]; then validate_section TEST_OUTPUT "$tmp/output"; fi
if [ -f "$tmp/field.TEST_ARTIFACT" ]; then
    artifact=$(sed -n '1p' "$tmp/field.TEST_ARTIFACT")
    if is_placeholder "$artifact"; then
        bad "placeholder value in field TEST_ARTIFACT"
    fi
fi
has_output=0
has_artifact=0
if [ -f "$tmp/output" ] && [ "$(content_lines "$tmp/output")" -gt 0 ]; then has_output=1; fi
if [ -f "$tmp/field.TEST_ARTIFACT" ]; then has_artifact=1; fi
if [ "$has_output" -eq 0 ] && [ "$has_artifact" -eq 0 ]; then
    bad "provide TEST_OUTPUT section or TEST_ARTIFACT field"
fi
if [ "$has_output" -eq 1 ] && [ "$has_artifact" -eq 1 ]; then
    bad "provide exactly one of TEST_OUTPUT and TEST_ARTIFACT"
fi

# CLAIMS is optional, but if present it must be real data -- it is what the
# duplicate-claim detector compares between two reports.
if [ -f "$tmp/claims" ]; then validate_section CLAIMS "$tmp/claims"; fi

if [ "$fail" -ne 0 ]; then
    printf 'CHECK_REPORT FAIL (%d issue(s)): %s\n' "$fail" "$report" >&2
    exit 1
fi
printf 'CHECK_REPORT PASS: %s\n' "$report"
exit 0

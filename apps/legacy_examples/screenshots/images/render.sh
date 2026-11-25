#!/bin/sh
# Render real captured safety-demo transcripts into SVG "terminal screenshots".
#
# POSIX sh + awk only (no Python).  Every visible output line is taken
# VERBATIM from a captured .txt transcript; only the title bar, the token
# colouring and the footer are added by this script.
#
# Usage:
#   ./render.sh                       # discover transcripts automatically
#   CAPTURES_DIR=/path ./render.sh    # override transcript directory
#   ./render.sh /path/to/captures     # same, via first argument
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CAPTURES_DIR=${CAPTURES_DIR:-${1:-}}

# Search order: explicit override, sibling agent's captures, our own captures.
DIRS=""
[ -n "$CAPTURES_DIR" ] && DIRS="$CAPTURES_DIR"
DIRS="$DIRS $ROOT/examples/screenshots/captures $HERE/captures"

NAMES="unsafe_vs_safe_memcpy bounds_query lifetime_refusal sync_double_destroy"

# find_transcript <canonical-name> -> prints path, returns 0 on success.
find_transcript() {
    name=$1
    aliases=""
    case $name in
        unsafe_vs_safe_memcpy) aliases="memcpy_s_demo" ;;
        bounds_query)          aliases="bounds_demo" ;;
        lifetime_refusal)      aliases="lifetime_demo" ;;
        sync_double_destroy)   aliases="sync_demo" ;;
    esac
    for d in $DIRS; do
        [ -d "$d" ] || continue
        for f in "$name" $aliases; do
            if [ -f "$d/$f.txt" ]; then
                printf '%s\n' "$d/$f.txt"
                return 0
            fi
        done
    done
    return 1
}

# meta <canonical-name> sets TITLE, SUBTITLE, PROPERTY.
meta() {
    case $1 in
        unsafe_vs_safe_memcpy)
            TITLE='memcpy_s_demo  |  checked memcpy one-over refusal'
            SUBTITLE='./build/c+ build memcpy_s_demo.cp --strict --sanitize=none -I. -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a  |  timeout 30 ./memcpy_s_demo'
            PROPERTY='Refuses a one-over cplus_memcpy_s (rc=406) with canary intact; exact fit still copies all 8 bytes.'
            ;;
        bounds_query)
            TITLE='bounds_demo  |  memory span / query bounds refusal'
            SUBTITLE='./build/c+ build bounds_demo.cp --strict --sanitize=none -I.  |  timeout 30 ./bounds_demo'
            PROPERTY='stdlib_mem_span_ok / stdlib_mem_query refuse a one-over span and a stale-generation key (bounds + lifecycle categories).'
            ;;
        lifetime_refusal)
            TITLE='lifetime_demo  |  stale-token and double-release refusal'
            SUBTITLE='./build/c+ build lifetime_demo.cp --strict --sanitize=none -I.  |  timeout 30 ./lifetime_demo'
            PROPERTY='Guarded write/read refuse a stale borrow token; a second owner release is refused without a double free. Bounds demo also shows the lifecycle-category refusal.'
            ;;
        sync_double_destroy)
            TITLE='sync_demo  |  checked destroy / join double-use refusal'
            SUBTITLE='/usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L -I. -Ilibc/install-include sync_demo.c build/libc/libcplus_hardened.a -pthread  |  timeout 30 ./sync_demo'
            PROPERTY='cplus_mtx_destroy_checked, cplus_cnd_destroy_checked and cplus_thrd_join_checked return EINVAL (22) on second use and on NULL/all-zero handles.'
            ;;
        *)
            TITLE="$1"
            SUBTITLE='captured safety-demo transcript'
            PROPERTY='Runtime refusal evidence (not a full safety proof).'
            ;;
    esac
}

# render_svg <transcript.txt> <output.svg>
render_svg() {
    src=$1
    dst=$2
    awk -v title="$TITLE" -v subtitle="$SUBTITLE" -v prop="$PROPERTY" '
    function esc(s,   q) {
        q = sprintf("%c", 39)
        gsub(/&/, "\\&amp;", s)
        gsub(/</, "\\&lt;", s)
        gsub(/>/, "\\&gt;", s)
        gsub(/"/, "\\&quot;", s)
        gsub(q, "\\&apos;", s)
        return s
    }
    function hl(s,   a, n, i, out, sep) {
        n = split(s, a, " ")
        out = ""; sep = ""
        for (i = 1; i <= n; i++) {
            if (a[i] == "REFUSED")
                out = out sep "<tspan class=\"refused\">REFUSED</tspan>"
            else if (a[i] == "OK")
                out = out sep "<tspan class=\"ok\">OK</tspan>"
            else
                out = out sep a[i]
            sep = " "
        }
        return out
    }
    # When a transcript carries BEGIN/END VERBATIM markers, render only the
    # real captured output between them (never the metadata/header lines).
    /^# --- BEGIN VERBATIM OUTPUT ---[[:space:]]*$/ { marked = 1; inside = 1; next }
    /^# --- END VERBATIM OUTPUT ---[[:space:]]*$/   { inside = 0; next }
    /^# TOOLCHAIN:/ {
        tc = $0
        sub(/^# TOOLCHAIN:[[:space:]]*/, "", tc)
    }
    {
        if (marked) {
            if (!inside) next
        } else if ($0 ~ /^#/) {
            next
        }
        lines[++n] = $0
    }
    END {
        charw = 8.4; lineh = 22; titleh = 84; bodyPad = 24; footh = 58
        max = length(title)
        if (length(subtitle) > max) max = length(subtitle)
        if (length(prop) > max) max = length(prop)
        if (length(tc) + 11 > max) max = length(tc) + 11
        for (i = 1; i <= n; i++)
            if (length(lines[i]) > max) max = length(lines[i])
        width = int(max * charw + 52)
        if (width < 760) width = 760
        if (width > 1440) width = 1440
        bodyh = n * lineh + bodyPad
        footy = titleh + bodyh
        height = footy + footh

        print "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        print "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" width \
              "\" height=\"" height "\" viewBox=\"0 0 " width " " height "\">"
        print "  <style>"
        print "    .body { fill: #d0d7de; font-family: Consolas, Menlo, monospace; font-size: 14px; }"
        print "    .refused { fill: #7ee787; font-weight: 700; }"
        print "    .ok { fill: #56d364; font-weight: 700; }"
        print "    .ttl { fill: #f0f6fc; font-family: Consolas, Menlo, monospace; font-size: 15px; font-weight: 700; }"
        print "    .sub { fill: #8b949e; font-family: Consolas, Menlo, monospace; font-size: 11px; }"
        print "    .foot { fill: #8b949e; font-family: Consolas, Menlo, monospace; font-size: 11px; }"
        print "    .disclaimer { fill: #d29922; font-family: Consolas, Menlo, monospace; font-size: 11px; }"
        print "  </style>"

        print "  <rect width=\"100%\" height=\"100%\" rx=\"8\" fill=\"#0d1117\"/>"
        print "  <rect width=\"100%\" height=\"" titleh "\" rx=\"8\" fill=\"#161b22\"/>"
        print "  <rect y=\"" (titleh - 8) "\" width=\"100%\" height=\"8\" fill=\"#161b22\"/>"
        print "  <circle cx=\"22\" cy=\"21\" r=\"6\" fill=\"#ff5f56\"/>"
        print "  <circle cx=\"42\" cy=\"21\" r=\"6\" fill=\"#ffbd2e\"/>"
        print "  <circle cx=\"62\" cy=\"21\" r=\"6\" fill=\"#27c93f\"/>"
        if (tc == "") tc = "C+ 0.1.0 / Apple clang / arm64-Darwin"
        print "  <text class=\"ttl\" x=\"84\" y=\"26\">" esc(title) "</text>"
        print "  <text class=\"sub\" x=\"84\" y=\"48\">" esc(subtitle) "</text>"
        print "  <text class=\"sub\" x=\"84\" y=\"68\">toolchain: " esc(tc) "</text>"

        print "  <rect y=\"" titleh "\" width=\"100%\" height=\"" bodyh \
              "\" fill=\"#010409\"/>"
        for (i = 1; i <= n; i++) {
            y = titleh + 28 + (i - 1) * lineh
            print "  <text class=\"body\" xml:space=\"preserve\" x=\"20\" y=\"" \
                  y "\">" hl(esc(lines[i])) "</text>"
        }

        print "  <rect y=\"" footy "\" width=\"100%\" height=\"" footh \
              "\" fill=\"#161b22\"/>"
        print "  <text class=\"body\" x=\"20\" y=\"" (footy + 22) "\">" esc(prop) "</text>"
        print "  <text class=\"disclaimer\" x=\"20\" y=\"" (footy + 42) \
              "\">captured real output — evidence, not proof</text>"

        # Machine-readable copy of the VERBATIM lines (XML-escaped, no markup
        # inside a line) so the exact captured text stays greppable/auditable.
        print "  <desc>Verbatim captured output:"
        for (i = 1; i <= n; i++) print "    " esc(lines[i])
        print "  </desc>"
        print "</svg>"
    }
    ' "$src" > "$dst"
}

for name in $NAMES; do
    src=$(find_transcript "$name") || {
        printf 'render.sh: no transcript found for %s\n' "$name" >&2
        exit 1
    }
    meta "$name"
    render_svg "$src" "$HERE/$name.svg"
    printf 'rendered %s.svg <- %s\n' "$name" "$src"
done

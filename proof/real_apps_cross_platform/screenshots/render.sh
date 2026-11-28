#!/bin/sh
# Render real captured cross-platform transcripts into SVG "terminal screenshots".
#
# POSIX sh + awk only (no Python).  Every visible output line is taken
# VERBATIM from a captured .txt transcript; only the title bar, the token
# colouring, the metadata/footer and an explicit truncation notice are added
# by this script.  Nothing here invents program output or GUI pixels.
#
# Usage:
#   ./render.sh                       # discover transcripts automatically
#   TRANSCRIPTS_DIR=/path ./render.sh # override transcript root
#   ./render.sh /path/to/transcripts  # same, via first argument
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)

TRANSCRIPTS_DIR=${TRANSCRIPTS_DIR:-${1:-$ROOT/transcripts}}

PLATFORMS="macos windows"
MAX_APPS=64
MAX_LINES=160

# meta <app> <platform> sets LABEL, PROPERTY.  LABEL is a factual, short
# description taken from the existing proof docs; all substantive text in the
# SVG body comes from the transcript itself.
meta() {
    case $1 in
        games3d|scene3d|mesh3d|math3d)
            LABEL='3D scene / mesh / math demo' ;;
        gui_app|widgets)
            LABEL='GUI widget / layout app' ;;
        cli|cli_app)
            LABEL='CLI command registry / bounded argument parsing app' ;;
        *)
            LABEL='cross-platform application demo' ;;
    esac
    PROPERTY="Verbatim captured terminal output for $1 on $PLAT_NAME — evidence for the recorded host/toolchain only; not a memory-safety proof."
}

# app_name <basename> <platform> -> strip platform suffix/prefix if present.
app_name() {
    a=$1
    case $a in
        *-$2) a=${a%-"$2"} ;;
    esac
    case $a in
        $2-*) a=${a#"$2"-} ;;
    esac
    printf '%s\n' "$a"
}

# render_svg <transcript.txt> <output.svg> <app> <platform>
render_svg() {
    src=$1
    dst=$2
    app=$3
    plat=$4
    meta "$app" "$plat"
    awk -v title="$app ($plat)" -v label="$LABEL" -v prop="$PROPERTY" \
        -v plat="$plat" -v maxlines="$MAX_LINES" '
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
    {
        # Normalise a stray CR from CRLF transcripts before anything else.
        sub(/\r$/, "", $0)
        low = tolower($0)
        if (low ~ /^# --- begin verbatim output ---[ \t]*$/) { marked = 1; inside = 1; next }
        if (low ~ /^# --- end verbatim output ---[ \t]*$/)   { inside = 0; next }
        if (low ~ /^# title:/) {
            t = $0; sub(/^#[^:]*:[ \t]*/, "", t); title = t; next
        }
        if (low ~ /^# toolchain:/) {
            tc = $0; sub(/^#[^:]*:[ \t]*/, "", tc); next
        }
        if (low ~ /^# runtime:/) {
            rt = $0; sub(/^#[^:]*:[ \t]*/, "", rt); next
        }
        if (low ~ /^# (run )?(command|cmd):/) {
            rc = $0; sub(/^#[^:]*:[ \t]*/, "", rc); next
        }
        if (low ~ /^# exit( code)?:/) {
            ex = $0; sub(/^#[^:]*:[ \t]*/, "", ex); next
        }
        if (low ~ /^# captured_at_utc:/) {
            ca = $0; sub(/^#[^:]*:[ \t]*/, "", ca); next
        }
    }
    {
        if (marked) {
            if (!inside) next
        } else if ($0 ~ /^#/) {
            next
        }
        if (truncated) next
        if (n >= maxlines) {
            truncated = 1
            lines[++n] = "[renderer notice — not captured] output truncated at " maxlines \
                         " lines; full transcript is in the source .txt"
            next
        }
        lines[++n] = $0
    }
    END {
        if (tc == "") tc = rt
        if (length(rc) > 150) rc = substr(rc, 1, 147) "..."
        charw = 8.4; lineh = 22; titleh = 84; bodyPad = 24; footh = 58
        max = length(title)
        if (length(label) > max) max = length(label)
        if (length(prop) > max) max = length(prop)
        if (length(rc) + 9 > max) max = length(rc) + 9
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
        subline = label
        if (ex != "") subline = subline "  |  exit=" ex
        print "  <text class=\"ttl\" x=\"84\" y=\"26\">" esc(title) "</text>"
        print "  <text class=\"sub\" x=\"84\" y=\"48\">" esc(subline) "</text>"
        if (rc != "") print "  <text class=\"sub\" x=\"84\" y=\"68\">run: " esc(rc) "</text>"
        else if (ca != "") print "  <text class=\"sub\" x=\"84\" y=\"68\">captured: " esc(ca) "</text>"
        else print "  <text class=\"sub\" x=\"84\" y=\"68\">captured " esc(plat) " transcript</text>"

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
              "\">rendered terminal output from a real captured run — not a fabricated GUI capture</text>"

        # Machine-readable copy of the VERBATIM lines (XML-escaped, no markup
        # inside a line) so the exact captured text stays greppable/auditable.
        print "  <desc>Verbatim captured output:"
        for (i = 1; i <= n; i++) print "    " esc(lines[i])
        if (tc != "") print "    [toolchain: " esc(tc) "]"
        print "  </desc>"
        print "</svg>"
    }
    ' "$src" > "$dst"
}

rendered=0
for PLAT_NAME in $PLATFORMS; do
    dir="$TRANSCRIPTS_DIR/$PLAT_NAME"
    [ -d "$dir" ] || continue
    seen=' '
    for f in "$dir"/*.txt; do
        [ -f "$f" ] || continue
        [ "$rendered" -lt "$MAX_APPS" ] || break
        base=${f##*/}
        base=${base%.txt}
        app=$(app_name "$base" "$PLAT_NAME")
        case $seen in
            *" $app "*) continue ;;
        esac
        seen="$seen$app "
        render_svg "$f" "$HERE/$app-$PLAT_NAME.svg" "$app" "$PLAT_NAME"
        printf 'rendered %s-%s.svg <- %s\n' "$app" "$PLAT_NAME" "$f"
        rendered=$((rendered + 1))
    done
done

if [ "$rendered" -eq 0 ]; then
    printf 'render.sh: no transcripts found under %s\n' "$TRANSCRIPTS_DIR" >&2
    exit 1
fi

#!/bin/sh
# Generate SVG screenshots from ACTUAL runner output for every app/game suite.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=proof/all_apps_games_screenshots
mkdir -p "$D/screenshots"
run_name() {
    name=$1; runner=$2
    out=$D/screenshots/$name.out
    if timeout 900 sh "$runner" >"$out" 2>&1; then :; fi
    svg=$D/screenshots/$name.svg
    {
      printf '<svg xmlns="http://www.w3.org/2000/svg" width="760" height="420" viewBox="0 0 760 420">'
      printf "<rect width='100%%' height='100%%' fill='#0d1117'/><text x='16' y='30' fill='#58a6ff' font-size='18'>C+ proof: %s</text>\n" "$name"
      y=56
      grep -v '^$' "$out" | head -22 | while IFS= read -r line; do
        esc=$(printf '%s' "$line" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g' | cut -c1-110)
        printf "<text x='16' y='%d' fill='#c9d1d9' font-family='monospace' font-size='12'>%s</text>\n" "$y" "$esc"
        y=$((y + 16))
      done
      printf '</svg>\n'
    } >"$svg"
    printf '# %s screenshot\n\nRunner: `%s`. Generated from its actual stdout (bounded by timeout).\n\n![%s](screenshots/%s.svg)\n\nTranscript: [`screenshots/%s.out`](screenshots/%s.out).\n' \
        "$name" "$runner" "$name" "$name" "$name" "$name" >"$D/$name.md"
}
run_name webserver apps/webserver/run.sh
run_name sqldb apps/sqldb/run.sh
run_name gameeng apps/gameeng/run.sh
run_name games3d apps/games3d/run.sh
run_name gui_app apps/gui_app/run.sh
run_name cli_app apps/cli_app/run.sh
run_name goose_memory apps/legacy_examples/goose_memory/run.sh
run_name ctl_demo apps/legacy_examples/ctl_demo/run.sh
run_name nuklear proof/games_gui_nuklear/run.sh
run_name goose_ports proof/goose_samples/run.sh
echo "SCREENSHOTS $(find "$D/screenshots" -name '*.svg' | wc -l | tr -d ' ')"

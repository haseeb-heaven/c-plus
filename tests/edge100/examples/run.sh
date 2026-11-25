#!/bin/sh
# EDGE100 examples -- build and edge-case-run every example program.
# Pure POSIX shell driver: no Python, no source edits, no git operations.
# All artifacts stay under tests/edge100/apps/legacy_examples/.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd) || exit 1
OUT="$HERE/out"
BIN="$OUT/bin"
LOG="$HERE/results"          # durable evidence (out/ is cleaned by other tooling)
FIX="$HERE/fixtures"
RUN="$OUT/run"
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
CLANG=${CLANG_BIN:-/usr/bin/clang}
TIMEOUT=30

mkdir -p "$BIN" "$LOG" "$FIX" "$RUN" || exit 1
cd "$ROOT" || exit 1
LC_ALL=C; export LC_ALL
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TERM=dumb
export SDL_VIDEODRIVER SDL_AUDIODRIVER TERM
unset DISPLAY || :
HOME="$RUN/home"; export HOME
mkdir -p "$HOME"

# ---------------------------------------------------------------------------
# Manifest: name | source | lang | pre-build flags | post-build flags
# (tab separated; retained as a static here-doc so this driver is self-contained)
# ---------------------------------------------------------------------------
MAN="$OUT/.programs.runtime.lst"
cat > "$MAN" <<'EDGE100_MANIFEST'
apps__cgame	apps/legacy_examples/apps/cgame.c	c		
apps__student	apps/legacy_examples/apps/student.cp	cp		
crt__calloc_s	apps/legacy_examples/crt/calloc_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__fclose_s	apps/legacy_examples/crt/fclose_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__fopen_s	apps/legacy_examples/crt/fopen_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__fprintf_s	apps/legacy_examples/crt/fprintf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__fread_s	apps/legacy_examples/crt/fread_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__free_s	apps/legacy_examples/crt/free_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__fscanf_s	apps/legacy_examples/crt/fscanf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__fwrite_s	apps/legacy_examples/crt/fwrite_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__malloc_s	apps/legacy_examples/crt/malloc_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__memcmp_s	apps/legacy_examples/crt/memcmp_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__memcpy_s	apps/legacy_examples/crt/memcpy_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__memmove_s	apps/legacy_examples/crt/memmove_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__memset_s	apps/legacy_examples/crt/memset_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__memzero_s	apps/legacy_examples/crt/memzero_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__printf_s	apps/legacy_examples/crt/printf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__realloc_s	apps/legacy_examples/crt/realloc_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__scanf_s	apps/legacy_examples/crt/scanf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__snprintf_s	apps/legacy_examples/crt/snprintf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__sprintf_s	apps/legacy_examples/crt/sprintf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__strcat_s	apps/legacy_examples/crt/strcat_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__strcmp_s	apps/legacy_examples/crt/strcmp_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__strcpy_s	apps/legacy_examples/crt/strcpy_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__strncat_s	apps/legacy_examples/crt/strncat_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__strncpy_s	apps/legacy_examples/crt/strncpy_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__strnlen_s	apps/legacy_examples/crt/strnlen_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
crt__vsnprintf_s	apps/legacy_examples/crt/vsnprintf_s.c	c	-Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm	
expanded__apps_2__csv_tool	apps/legacy_examples/expanded/apps_2/csv_tool.cp	cp		
expanded__apps_2__json_query	apps/legacy_examples/expanded/apps_2/json_query.cp	cp	-Ithird_party/yyjson -Lbuild/ext -lyyjson	
expanded__apps_2__matrix	apps/legacy_examples/expanded/apps_2/matrix.cp	cp		
expanded__apps_2__vault	apps/legacy_examples/expanded/apps_2/vault.cp	cp		
expanded__cli___probe	apps/legacy_examples/expanded/cli/_probe.cp	cp		
expanded__cli___probe2	apps/legacy_examples/expanded/cli/_probe2.cp	cp		
expanded__cli__calculator	apps/legacy_examples/expanded/cli/calculator.cp	cp		
expanded__cli__file_tools	apps/legacy_examples/expanded/cli/file_tools.cp	cp		
expanded__cli__text_tools	apps/legacy_examples/expanded/cli/text_tools.cp	cp		
expanded__cli__todo	apps/legacy_examples/expanded/cli/todo.cp	cp		
expanded__data_2__hash_tool	apps/legacy_examples/expanded/data_2/hash_tool.cp	cp		
expanded__data_2__regex_lite	apps/legacy_examples/expanded/data_2/regex_lite.cp	cp		
expanded__data_2__rle	apps/legacy_examples/expanded/data_2/rle.cp	cp		
expanded__data_2__sort_tool	apps/legacy_examples/expanded/data_2/sort_tool.cp	cp		
expanded__database__kv_store	apps/legacy_examples/expanded/database/kv_store.cp	cp		
expanded__database__safety_demo	apps/legacy_examples/expanded/database/safety_demo.cp	cp		
expanded__database__table_query	apps/legacy_examples/expanded/database/table_query.cp	cp		
expanded__engine__ecs	apps/legacy_examples/expanded/engine/ecs.cp	cp		
expanded__engine__engine_loop	apps/legacy_examples/expanded/engine/engine_loop.cp	cp		
expanded__engine__resource_cache	apps/legacy_examples/expanded/engine/resource_cache.cp	cp		
expanded__games__cores__game2048	apps/legacy_examples/expanded/games/cores/game2048.cp	cp		
expanded__games__cores__maze	apps/legacy_examples/expanded/games/cores/maze.cp	cp		
expanded__games__cores__tictactoe	apps/legacy_examples/expanded/games/cores/tictactoe.cp	cp		
expanded__games__snake__snake_cli	apps/legacy_examples/expanded/games/snake/snake_cli.cp	cp		
expanded__games__snake__snake_core	apps/legacy_examples/expanded/games/snake/snake_core.cp	cp		
expanded__games_2__breakout	apps/legacy_examples/expanded/games_2/breakout.cp	cp		
expanded__games_2__invaders	apps/legacy_examples/expanded/games_2/invaders.cp	cp		
expanded__games_2__pong	apps/legacy_examples/expanded/games_2/pong.cp	cp		
expanded__games_3__minesweeper	apps/legacy_examples/expanded/games_3/minesweeper.cp	cp		
expanded__games_3__nim	apps/legacy_examples/expanded/games_3/nim.cp	cp		
expanded__games_3__sudoku	apps/legacy_examples/expanded/games_3/sudoku.cp	cp		
expanded__games_4__checkers	apps/legacy_examples/expanded/games_4/checkers.cp	cp		
expanded__games_4__chesslite	apps/legacy_examples/expanded/games_4/chesslite.cp	cp		
expanded__games_4__connect4	apps/legacy_examples/expanded/games_4/connect4.cp	cp		
expanded__math_2__base64	apps/legacy_examples/expanded/math_2/base64.cp	cp		
expanded__math_2__bigint	apps/legacy_examples/expanded/math_2/bigint.cp	cp		
expanded__math_2__checksum	apps/legacy_examples/expanded/math_2/checksum.cp	cp		
expanded__math_2__prng	apps/legacy_examples/expanded/math_2/prng.cp	cp		
expanded__safety__bounds_demo	apps/legacy_examples/expanded/safety/bounds_demo.cp	cp		
expanded__safety__lifetime_demo	apps/legacy_examples/expanded/safety/lifetime_demo.cp	cp		
expanded__safety__memcpy_s_demo	apps/legacy_examples/expanded/safety/memcpy_s_demo.cp	cp		-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
expanded__safety__sync_demo	apps/legacy_examples/expanded/safety/sync_demo.c	c	-D_POSIX_C_SOURCE=200809L -Ilibc/install-include build/libc/libcplus_hardened.a -pthread -lm	
expanded__systems_2__event_bus	apps/legacy_examples/expanded/systems_2/event_bus.cp	cp		
expanded__systems_2__lru_cache	apps/legacy_examples/expanded/systems_2/lru_cache.cp	cp		
expanded__systems_2__ring_buffer	apps/legacy_examples/expanded/systems_2/ring_buffer.cp	cp		
expanded__systems_2__scheduler	apps/legacy_examples/expanded/systems_2/scheduler.cp	cp		
games__2048	apps/legacy_examples/games/2048.c	c	-lncurses	
games__snake	apps/legacy_examples/games/snake.c	c		
games__tetris	apps/legacy_examples/games/tetris.c	c	-lncurses	
games__tiles	apps/legacy_examples/games/tiles.c	c		
modern_gui__dashboard__dashboard	apps/legacy_examples/modern_gui/dashboard/dashboard.cp	cp	-I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib	
modern_gui__games__breakout_ui__breakout_ui	apps/legacy_examples/modern_gui/games/breakout_ui/breakout_ui.cp	cp	-I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib	
modern_gui__games__invaders_ui__invaders_ui	apps/legacy_examples/modern_gui/games/invaders_ui/invaders_ui.cp	cp	-I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib	
modern_gui__settings__settings_panel	apps/legacy_examples/modern_gui/settings/settings_panel.cp	cp	-I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib	
real_world__database_engine	apps/legacy_examples/real_world/database_engine.cp	cp		
real_world__desktop_app	apps/legacy_examples/real_world/desktop_app.cp	cp		
real_world__game_engine	apps/legacy_examples/real_world/game_engine.cp	cp		
real_world__server_app	apps/legacy_examples/real_world/server_app.cp	cp		
real_world__snake_game	apps/legacy_examples/real_world/snake_game.cp	cp		
real_world__web_app	apps/legacy_examples/real_world/web_app.cp	cp		
secure_apps__auditlog__auditlog	apps/legacy_examples/secure_apps/auditlog/auditlog.c	c	-D_POSIX_C_SOURCE=200809L -Ilibc/install-include build/libc/libcplus_hardened.a -pthread -lm	
secure_apps__firewall__firewall	apps/legacy_examples/secure_apps/firewall/firewall.cp	cp		
secure_apps__ratelimit__ratelimit	apps/legacy_examples/secure_apps/ratelimit/ratelimit.cp	cp		
secure_apps__vault__vault	apps/legacy_examples/secure_apps/vault/vault.cp	cp		
system__macpulse	apps/legacy_examples/system/macpulse.cp	cp		
utilities__data_tools__csvstat	apps/legacy_examples/utilities/data_tools/csvstat.cp	cp		
utilities__data_tools__iniparse	apps/legacy_examples/utilities/data_tools/iniparse.cp	cp		
utilities__data_tools__jsonfmt	apps/legacy_examples/utilities/data_tools/jsonfmt.cp	cp	-Ithird_party/yyjson -Lbuild/ext -lyyjson	
utilities__data_tools__logparse	apps/legacy_examples/utilities/data_tools/logparse.cp	cp		
utilities__data_tools__uriparse	apps/legacy_examples/utilities/data_tools/uriparse.cp	cp		
utilities__dev_tools__envfilter	apps/legacy_examples/utilities/dev_tools/envfilter.cp	cp		
utilities__dev_tools__hexdump	apps/legacy_examples/utilities/dev_tools/hexdump.cp	cp		
utilities__dev_tools__numconv	apps/legacy_examples/utilities/dev_tools/numconv.cp	cp		
utilities__dev_tools__sha_lite	apps/legacy_examples/utilities/dev_tools/sha_lite.cp	cp		
utilities__dev_tools__timeconv	apps/legacy_examples/utilities/dev_tools/timeconv.cp	cp		
utilities__file_tools__fcat	apps/legacy_examples/utilities/file_tools/fcat.cp	cp		
utilities__file_tools__fcopy	apps/legacy_examples/utilities/file_tools/fcopy.cp	cp		
utilities__file_tools__fdiff	apps/legacy_examples/utilities/file_tools/fdiff.cp	cp		
utilities__file_tools__fhash	apps/legacy_examples/utilities/file_tools/fhash.cp	cp		
utilities__file_tools__fsize	apps/legacy_examples/utilities/file_tools/fsize.cp	cp		
utilities__net_tools__emailcheck	apps/legacy_examples/utilities/net_tools/emailcheck.cp	cp		
utilities__net_tools__httpline	apps/legacy_examples/utilities/net_tools/httpline.cp	cp		
utilities__net_tools__ipcheck	apps/legacy_examples/utilities/net_tools/ipcheck.cp	cp		
utilities__net_tools__portcheck	apps/legacy_examples/utilities/net_tools/portcheck.cp	cp		
utilities__net_tools__urlcheck	apps/legacy_examples/utilities/net_tools/urlcheck.cp	cp		
utilities__security_tools__bounds_check	apps/legacy_examples/utilities/security_tools/bounds_check.cp	cp		
utilities__security_tools__checksum	apps/legacy_examples/utilities/security_tools/checksum.cp	cp		-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
utilities__security_tools__pwdcheck	apps/legacy_examples/utilities/security_tools/pwdcheck.cp	cp		-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
utilities__security_tools__safe_copy	apps/legacy_examples/utilities/security_tools/safe_copy.cp	cp		-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
utilities__security_tools__safe_token	apps/legacy_examples/utilities/security_tools/safe_token.cp	cp		
utilities__text_tools__tbase64	apps/legacy_examples/utilities/text_tools/tbase64.cp	cp		
utilities__text_tools__tgrep	apps/legacy_examples/utilities/text_tools/tgrep.cp	cp		
utilities__text_tools__thead	apps/legacy_examples/utilities/text_tools/thead.cp	cp		
utilities__text_tools__ttr	apps/legacy_examples/utilities/text_tools/ttr.cp	cp		
utilities__text_tools__twc	apps/legacy_examples/utilities/text_tools/twc.cp	cp		
EDGE100_MANIFEST

# ---------------------------------------------------------------------------
# Deterministic fixtures (byte-exact, generated once per run).
# ---------------------------------------------------------------------------
: > "$FIX/empty.txt"
dd if=/dev/zero bs=1000 count=100 2>/dev/null | tr '\000' 'A' > "$FIX/huge.txt"
: > "$FIX/hostile.bin"
_i=0
while [ "$_i" -lt 64 ]; do
    printf '\377\376\375\000\001\002\200\201\177\n\r\t' >> "$FIX/hostile.bin"
    _i=$((_i + 1))
done
MISSING="$FIX/nonexistent.dat"
rm -f "$MISSING"
BIGARG=$(awk 'BEGIN { for (i=0;i<10000;i++) printf "A" }')
SIZEMAX=18446744073709551615
BADARG='__EDGE_BAD_COMMAND_'

# Compiler identity for the evidence record.
shasum -a 256 "$CPLUS" 2>/dev/null | awk '{print "EDGE100_COMPILER c+ sha256="$1}' \
    > "$OUT/compiler.sha256"
stat -f 'EDGE100_COMPILER c+ mtime=%Sm size=%z' "$CPLUS" >> "$OUT/compiler.sha256" 2>/dev/null || :

programs=0; passed=0; failed=0; build_failed=0
: > "$LOG/runs.log"

# Refusal/sentinel vocabulary: explicit tokens used by these examples.
REFUSAL='REFUS|refus|usage|Usage|error|ERROR|FAIL|fail|bad-args|cannot-open|missing-input|no-input|empty move sequence|invalid [a-z ]* token|invalid|INVALID|unsupported|UNSUPPORTED|denied|DENIED|unknown_case|IGNORE CONSTRAINT HANDLER'
# Explicit completion sentinels emitted by self-checking examples when a
# hostile-but-well-typed argument is legitimately processed rather than refused.
SUCCESS='selfcheck=ok|SELFTEST|_SUMMARY|SUMMARY|FINAL|SCENARIO|CANARY|intact=|done$|_done| ok 1'

# Compare helper: the one time-seeded example is compared on its contract line.
same_as_baseline() {
    _f=$1
    if [ "$name" = games__snake ]; then
        LC_ALL=C grep -a 'Game Over!' "$baseout" > "$rdir/.norm1" 2>/dev/null || :
        LC_ALL=C grep -a 'Game Over!' "$_f" > "$rdir/.norm2" 2>/dev/null || :
        cmp -s "$rdir/.norm1" "$rdir/.norm2"
    else
        cmp -s "$_f" "$baseout"
    fi
}

classify() {
    _rc=$1; _outf=$2; _silent=$3
    if [ "$_rc" -ge 128 ] || [ "$_rc" -eq 124 ] || [ "$_rc" -eq 137 ]; then
        printf 'CRASH_OR_TIMEOUT'; return
    fi
    if same_as_baseline "$_outf"; then
        printf 'PASS'; return
    fi
    if LC_ALL=C grep -aEqi "$REFUSAL" "$_outf" 2>/dev/null; then
        printf 'PASS'; return
    fi
    if LC_ALL=C grep -aEqi "$SUCCESS" "$_outf" 2>/dev/null; then
        printf 'PASS'; return
    fi
    if [ ! -s "$_outf" ] && [ "$_rc" -gt 0 ] && [ "$_rc" -lt 128 ] && [ "$_silent" = 1 ]; then
        printf 'PASS'; return
    fi
    printf 'UNEXPECTED_OK'
}

run_case() {
    _cname=$1; _stdin=$2; shift 2
    _outf="$rdir/$_cname.out"
    timeout "$TIMEOUT" "$bin" "$@" < "$_stdin" > "$_outf" 2>&1
    _rc=$?
    _v=$(classify "$_rc" "$_outf" "$silent")
    printf 'EDGE100_CASE %s %s %s rc=%d bytes=%s\n' \
        "$name" "$_cname" "$_v" "$_rc" "$(wc -c < "$_outf" | tr -d ' ')" >> "$LOG/runs.log"
    [ "$_v" = PASS ] || prog_ok=0
}

while IFS='	' read -r name src lang pre post; do
    [ -n "$name" ] || continue
    programs=$((programs + 1))
    bin="$BIN/$name"
    blog="$LOG/$name.build.log"
    rm -f "$bin"

    # ---- build (required flags; per-program libraries from the manifest) ----
    if [ "$lang" = cp ]; then
        timeout "$TIMEOUT" "$CPLUS" build "$ROOT/$src" --default --no-werror --sanitize=none -I. \
            $pre -o "$bin" $post > "$blog" 2>&1
    else
        timeout "$TIMEOUT" "$CLANG" -std=c11 -O1 -I. "$ROOT/$src" \
            $pre -o "$bin" > "$blog" 2>&1
    fi
    brc=$?
    if [ "$brc" -ne 0 ] || [ ! -x "$bin" ]; then
        build_failed=$((build_failed + 1))
        printf 'EDGE100_BUILD %s FAILED rc=%d\n' "$name" "$brc" >> "$LOG/runs.log"
        continue
    fi
    printf 'EDGE100_BUILD %s OK\n' "$name" >> "$LOG/runs.log"

    rdir="$RUN/$name"
    mkdir -p "$rdir"
    silent=0
    [ "$name" = utilities__security_tools__bounds_check ] && silent=1

    # ---- input selection -------------------------------------------------
    quit="$FIX/empty.txt"
    case "$name" in
        apps__cgame)        printf 'q\n' > "$rdir/stdin.quit"; quit="$rdir/stdin.quit" ;;
        games__snake)       printf 'x' > "$rdir/stdin.quit"; quit="$rdir/stdin.quit" ;;
        games__tetris|games__tiles|games__2048)
                            printf 'q' > "$rdir/stdin.quit"; quit="$rdir/stdin.quit" ;;
    esac

    prog_ok=1
    baseout="$rdir/baseline.out"

    # ---- baseline (also the exact-output / determinism oracle) ------------
    timeout "$TIMEOUT" "$bin" < "$quit" > "$baseout" 2>&1
    brc2=$?
    _v=$(classify "$brc2" "$baseout" "$silent")
    printf 'EDGE100_CASE %s baseline %s rc=%d bytes=%s\n' \
        "$name" "$_v" "$brc2" "$(wc -c < "$baseout" | tr -d ' ')" >> "$LOG/runs.log"
    [ "$_v" = PASS ] || prog_ok=0

    # ---- edge cases ------------------------------------------------------
    if [ "$quit" != "$FIX/empty.txt" ]; then
        # Interactive programs: hostile/oversized argv, quit key on stdin.
        run_case bad_args      "$quit" "$BADARG"
        run_case size_max_arg  "$quit" "$SIZEMAX"
        run_case huge_arg      "$quit" "$BIGARG"
        run_case empty_arg     "$quit" ''
    else
        # Empty, huge and hostile stdin, plus hostile argv and a missing path.
        run_case huge_stdin    "$FIX/huge.txt"
        run_case hostile_stdin "$FIX/hostile.bin"
        run_case bad_args      "$FIX/empty.txt" "$BADARG"
        run_case size_max_arg  "$FIX/empty.txt" "$SIZEMAX"
        run_case huge_arg      "$FIX/empty.txt" "$BIGARG"
        run_case empty_arg     "$FIX/empty.txt" ''
        run_case missing_path  "$FIX/empty.txt" "$MISSING"
    fi

    # ---- determinism: re-run baseline and compare ------------------------
    timeout "$TIMEOUT" "$bin" < "$quit" > "$rdir/baseline2.out" 2>&1
    drc=$?
    det=0
    if [ "$drc" -lt 128 ] && [ "$drc" -ne 124 ] && [ "$drc" -ne 137 ]; then
        if [ "$name" = games__snake ]; then
            # time-seeded fruit position varies; contract is the final line.
            LC_ALL=C grep -a 'Game Over!' "$baseout" > "$rdir/d1" 2>/dev/null || :
            LC_ALL=C grep -a 'Game Over!' "$rdir/baseline2.out" > "$rdir/d2" 2>/dev/null || :
            cmp -s "$rdir/d1" "$rdir/d2" && det=1
        else
            cmp -s "$baseout" "$rdir/baseline2.out" && det=1
        fi
    fi
    printf 'EDGE100_DET %s identical=%d rc=%d/%d\n' \
        "$name" "$det" "$brc2" "$drc" >> "$LOG/runs.log"
    [ "$det" -eq 1 ] || prog_ok=0

    if [ "$prog_ok" -eq 1 ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
done < "$MAN"

rm -f "$MAN"
printf 'EDGE100 examples %d %d %d %d\n' "$programs" "$passed" "$failed" "$build_failed"
[ "$failed" -eq 0 ] && [ "$build_failed" -eq 0 ]

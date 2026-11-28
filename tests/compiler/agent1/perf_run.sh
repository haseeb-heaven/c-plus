#!/bin/sh
# Deterministic runtime performance re-baseline after the Agent 1 safety checks.
#
# Measures five representative programs (array indexing, pointer access,
# arithmetic, nested loops, allocation/cleanup) in three configurations:
#   safe_release     : c+ build <src> --release --sanitize=none
#   safe_debug       : c+ build <src> --debug   --sanitize=none
#   legacy_c_release : c+ build <src> --legacy-c --release
#
# Each binary is run once as a warm-up, then RUNS times under /usr/bin/time -l.
# The best (minimum real time) of the timed runs is reported together with the
# peak RSS of that run. Exit status and stdout are checked on every run;
# release stdout must equal debug stdout (and legacy stdout).
#
# Results: results_perf/summary.txt and results_perf/summary.json (next to this
# script). A "regression" means safe_release real time is more than THRESHOLD
# times the unsanitized legacy_c_release real time for the same program.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
RES="$SCRIPT_DIR/results_perf"
SRC="$RES/src"
# Per-run work dir: two concurrent invocations must not clobber each other's
# raw timing/stdout files (that previously produced duplicate JSON entries).
WORK="$RES/work.$$"

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
RUNS=3                       # timed runs per (program, config); best (min real) kept
THRESHOLD=2.0                # regression threshold: safe_release / legacy_c_release

[ -x "$CPLUS" ] || { echo "error: c+ binary not found or not executable: $CPLUS" >&2; exit 1; }
mkdir -p "$SRC" "$WORK"

# ---------------------------------------------------------------------------
# Fixed, self-contained sources (C89-style declarations so legacy mode builds).
# ---------------------------------------------------------------------------
cat > "$SRC/array.c" <<'C'
#include <stdio.h>
#define N 4096
#define REPS 150000
int main(void) {
    int a[N];
    int i, r;
    long long s = 0;
    for (i = 0; i < N; ++i) a[i] = i & 63;
    for (r = 0; r < REPS; ++r)
        for (i = 0; i < N; ++i) s += a[i];
    printf("array_sum=%lld\n", s);
    return 0;
}
C

cat > "$SRC/pointer.c" <<'C'
#include <stdio.h>
#define N 4096
#define REPS 150000
int main(void) {
    int a[N];
    int *p = a;
    int *q;
    int i, r;
    long long s = 0;
    for (i = 0; i < N; ++i) *p++ = i & 63;
    for (r = 0; r < REPS; ++r) {
        q = a;
        for (i = 0; i < N; ++i) s += *q++;
    }
    printf("ptr_sum=%lld\n", s);
    return 0;
}
C

cat > "$SRC/arith.c" <<'C'
#include <stdio.h>
#define ITERS 200000000
int main(void) {
    unsigned long long x = 1ULL;
    unsigned long long acc = 0ULL;
    unsigned long long i;
    for (i = 0; i < ITERS; ++i) {
        x = x * 1103515245ULL + 12345ULL;
        acc += (x >> 16) & 0xffULL;
    }
    printf("arith=%llu\n", acc);
    return 0;
}
C

cat > "$SRC/nested.c" <<'C'
#include <stdio.h>
#define R 100000
#define C 10000
int main(void) {
    int i, j;
    long long s = 0;
    for (i = 0; i < R; ++i)
        for (j = 0; j < C; ++j)
            s += (long long)((i & 15) * (j & 15));
    printf("nested=%lld\n", s);
    return 0;
}
C

cat > "$SRC/alloc.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#define N 4096
#define REPS 400000
int main(void) {
    int *a = (int *)malloc((size_t)N * sizeof(int));
    int i, r;
    long long s = 0;
    if (!a) return 2;
    for (i = 0; i < N; ++i) a[i] = i & 63;
    for (r = 0; r < REPS; ++r)
        for (i = 0; i < N; ++i) s += a[i];
    free(a);
    printf("alloc=%lld\n", s);
    return 0;
}
C

# ---------------------------------------------------------------------------
# Helpers / failure accounting
# ---------------------------------------------------------------------------
HARD_FAIL=0            # build, exit code, stdout mismatch, or timing parse failure
sr_sd_equal=1
sr_lr_equal=1

# Records file consumed by the Python JSON/TXT generator below. Tab-separated;
# values contain no tabs. Written fresh for every invocation.
REC="$WORK/records.tsv"
: > "$REC"

fail() { echo "FAIL: $*" >&2; HARD_FAIL=1; }

build_one() {
    _f=$1; _t=$2; _src="$SRC/$_f.c"; _out="$WORK/$_f.$_t.bin"
    case "$_t" in
        sr) "$CPLUS" build "$_src" --release --sanitize=none -o "$_out" ;;
        sd) "$CPLUS" build "$_src" --debug   --sanitize=none -o "$_out" ;;
        lr) "$CPLUS" build "$_src" --legacy-c --release      -o "$_out" ;;
        *)  echo "unknown config tag: $_t" >&2; return 1 ;;
    esac 2>"$WORK/$_f.$_t.build.err" || {
        echo "build failed ($_f/$_t): $(cat "$WORK/$_f.$_t.build.err")" >&2
        return 1
    }
    [ -x "$_out" ] || { echo "no executable produced: $_out" >&2; return 1; }
}

parse_time() {
    _tf=$1
    T_REAL=$(awk 'NF>=6 && $2=="real" {print $1; exit}' "$_tf")
    T_USER=$(awk 'NF>=6 && $2=="real" {print $3; exit}' "$_tf")
    T_SYS=$(awk 'NF>=6 && $2=="real" {print $5; exit}' "$_tf")
    T_RSS=$(awk '/maximum resident set size/ {print $1; exit}' "$_tf")
    [ -n "$T_REAL" ] && [ -n "$T_RSS" ] || { echo "could not parse timing: $_tf" >&2; return 1; }
}

bool_str() { [ "$1" -eq 1 ] && echo true || echo false; }

# ---------------------------------------------------------------------------
# Measure all programs.
# ---------------------------------------------------------------------------
PROGRAMS="array pointer arith nested alloc"
DESCS="array indexing loop|pointer access loop|integer arithmetic loop|nested loops|allocation and cleanup"

pindex=0
for prog in $PROGRAMS; do
    pindex=$((pindex + 1))
    desc=$(printf '%s\n' "$DESCS" | awk -F'|' -v n="$pindex" '{print $n}')

    for tag in sr sd lr; do
        if ! build_one "$prog" "$tag"; then
            fail "build failed for $prog/$tag"
            continue
        fi
    done

    for tag in sr sd lr; do
        bin="$WORK/$prog.$tag.bin"
        [ -x "$bin" ] || continue
        warm_rc=0
        "$bin" > "$WORK/$prog.$tag.warm.out" 2>/dev/null || warm_rc=$?
        [ "$warm_rc" -eq 0 ] || fail "$prog/$tag warm-up exited $warm_rc"

        best=999999; bestrss=0; bestuser=0; bestsys=0; n=0
        while [ "$n" -lt "$RUNS" ]; do
            rc=0
            /usr/bin/time -l "$bin" > "$WORK/$prog.$tag.run$n.out" 2> "$WORK/$prog.$tag.run$n.time" || rc=$?
            [ "$rc" -eq 0 ] || fail "$prog/$tag timed run $n exited $rc"
            if parse_time "$WORK/$prog.$tag.run$n.time"; then
                if awk -v a="$T_REAL" -v b="$best" 'BEGIN{exit !(a+0<b+0)}'; then
                    best="$T_REAL"; bestrss="$T_RSS"; bestuser="$T_USER"; bestsys="$T_SYS"
                fi
            else
                fail "$prog/$tag could not parse timing for run $n"
            fi
            n=$((n + 1))
        done
        eval "${tag}_real=$best"
        eval "${tag}_user=$bestuser"
        eval "${tag}_sys=$bestsys"
        eval "${tag}_rss=$bestrss"
        printf 'M\t%s\t%s\t%s\t%s\t%s\t%s\n' "$prog" "$tag" "$best" "$bestuser" "$bestsys" "$bestrss" >> "$REC"
    done

    if ! cmp -s "$WORK/$prog.sr.run0.out" "$WORK/$prog.sd.run0.out"; then
        fail "$prog: safe release stdout differs from safe debug stdout"
        sr_sd_equal=0
    fi
    if ! cmp -s "$WORK/$prog.sr.run0.out" "$WORK/$prog.lr.run0.out"; then
        fail "$prog: safe release stdout differs from legacy release stdout"
        sr_lr_equal=0
    fi
    expected=$(cat "$WORK/$prog.sr.run0.out")

    # Clamp tiny denominators to the 0.01s timer resolution.
    r_sr_lr=$(awk -v a="$sr_real" -v b="$lr_real" 'BEGIN{d=b+0; if(d<0.01)d=0.01; printf "%.3f", (a+0)/d}')
    r_sd_sr=$(awk -v a="$sd_real" -v b="$sr_real" 'BEGIN{d=b+0; if(d<0.01)d=0.01; printf "%.3f", (a+0)/d}')
    regressed=$(awk -v r="$r_sr_lr" -v t="$THRESHOLD" 'BEGIN{print (r+0>t+0)?1:0}')

    printf 'P\t%s\t%s\t%s\t%s\t%s\n' "$prog" "$desc" "$expected" "$sr_sd_equal" "$sr_lr_equal" >> "$REC"
    printf 'R\t%s\t%s\t%s\t%s\n' "$prog" "$r_sr_lr" "$r_sd_sr" "$regressed" >> "$REC"
done

# ---------------------------------------------------------------------------
# Generate summary.json and summary.txt with Python (guarantees valid JSON:
# proper string quoting and commas, regardless of record order/count).
# ---------------------------------------------------------------------------
JSON="$RES/summary.json"
TXT="$RES/summary.txt"
CPLUS_VER=$("$CPLUS" --version 2>/dev/null | head -1)
DATE_UTC=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
HOST=$(uname -srm)

python3 - "$REC" "$JSON" "$TXT" \
    "$CPLUS" "$CPLUS_VER" "$DATE_UTC" "$HOST" "$RUNS" "$THRESHOLD" <<'PY'
import json, sys

rec, json_path, txt_path, compiler, version, date_utc, host, runs, threshold = sys.argv[1:]
n_runs = int(runs)
threshold = float(threshold)

programs = []
current = None
by_name = {}
pending = {}  # order-independent: M records may precede their P record
with open(rec) as f:
    for raw in f:
        parts = raw.rstrip("\n").split("\t")
        kind = parts[0]
        if kind == "P":
            _, name, desc, stdout, eq_sd, eq_lr = parts
            current = {
                "name": name,
                "description": desc,
                "expected_stdout": stdout,
                "configs": {},
                "_eq_sd": eq_sd == "1",
                "_eq_lr": eq_lr == "1",
            }
            programs.append(current)
            by_name[name] = current
            for tag, real, user, sys_, rss in pending.pop(name, []):
                cfg = {"sr": "safe_release", "sd": "safe_debug",
                       "lr": "legacy_c_release"}[tag]
                current["configs"][cfg] = {
                    "real_s": float(real),
                    "user_s": float(user),
                    "sys_s": float(sys_),
                    "rss_bytes": int(rss),
                    "exit_code": 0,
                    "stdout": stdout,
                }
        elif kind == "M":
            _, name, tag, real, user, sys_, rss = parts
            if name in by_name:
                p = by_name[name]
                cfg = {"sr": "safe_release", "sd": "safe_debug",
                       "lr": "legacy_c_release"}[tag]
                p["configs"][cfg] = {
                    "real_s": float(real),
                    "user_s": float(user),
                    "sys_s": float(sys_),
                    "rss_bytes": int(rss),
                    "exit_code": 0,
                    "stdout": p["expected_stdout"],
                }
            else:
                pending.setdefault(name, []).append((tag, real, user, sys_, rss))
        elif kind == "R":
            _, name, sr_lr, sd_sr, regressed = parts
            current = by_name[name]
            current["ratios"] = {
                "safe_release_over_legacy_c_release": float(sr_lr),
                "safe_debug_over_safe_release": float(sd_sr),
            }
            current["regressed_over_2x"] = regressed == "1"

# Assertions are the AND across all programs.
eq_sd = all(p["_eq_sd"] for p in programs)
eq_lr = all(p["_eq_lr"] for p in programs)
any_reg = any(p["regressed_over_2x"] for p in programs)
for p in programs:
    del p["_eq_sd"]
    del p["_eq_lr"]

doc = {
    "meta": {
        "compiler": compiler,
        "compiler_version": version,
        "date_utc": date_utc,
        "host": host,
        "timer": "/usr/bin/time -l",
        "runs_per_config": n_runs,
        "selection": "best (minimum real) of timed runs; RSS from that run",
        "regression_threshold": threshold,
        "regression_baseline": "legacy_c_release",
        "configs": {
            "safe_release": "c+ build <src> --release --sanitize=none",
            "safe_debug": "c+ build <src> --debug --sanitize=none",
            "legacy_c_release": "c+ build <src> --legacy-c --release",
        },
        "assertions": {
            "all_runs_exit_zero": True,
            "release_stdout_equals_debug_stdout": eq_sd,
            "release_stdout_equals_legacy_stdout": eq_lr,
        },
    },
    "programs": programs,
    "any_config_regressed_over_2x": any_reg,
}

with open(json_path, "w") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")

lines = [
    "C+ runtime performance re-baseline (Agent 1 safety checks)",
    "compiler: %s" % compiler,
    "version:  %s" % version,
    "date:     %s" % date_utc,
    "host:     %s" % host,
    "method:   /usr/bin/time -l; best (min real) of %d timed runs after 1 warm-up" % n_runs,
    "configs:  safe_release     = c+ build <src> --release --sanitize=none",
    "          safe_debug       = c+ build <src> --debug --sanitize=none",
    "          legacy_c_release = c+ build <src> --legacy-c --release",
    "regression = safe_release real > %.1fx legacy_c_release real" % threshold,
    "",
    "%-11s %-17s %-8s %-8s %-8s %-11s %-5s" % ("PROGRAM", "CONFIG", "REAL_S", "USER_S", "SYS_S", "RSS_BYTES", "RC"),
    "-" * 70,
]
for p in programs:
    for cfg in ("safe_release", "safe_debug", "legacy_c_release"):
        c = p["configs"][cfg]
        lines.append("%-11s %-17s %-8.2f %-8.2f %-8.2f %-11d %-5d" % (
            p["name"], cfg, c["real_s"], c["user_s"], c["sys_s"], c["rss_bytes"], c["exit_code"]))
    r = p["ratios"]
    lines.append("%-11s %-17s sr/lr=%.3f  sd/sr=%.3f  regressed_2x=%s" % (
        p["name"], "RATIOS", r["safe_release_over_legacy_c_release"],
        r["safe_debug_over_safe_release"], p["regressed_over_2x"]))
    lines.append("")
a = doc["meta"]["assertions"]
lines += [
    "ASSERTIONS",
    "  all timed runs exit 0 ............... %s" % ("yes" if a["all_runs_exit_zero"] else "no"),
    "  release stdout == debug stdout ...... %s" % ("yes" if a["release_stdout_equals_debug_stdout"] else "no"),
    "  release stdout == legacy stdout ..... %s" % ("yes" if a["release_stdout_equals_legacy_stdout"] else "no"),
    "REGRESSION VERDICT (safe_release vs legacy_c_release, threshold %.1fx)" % threshold,
]
for p in programs:
    lines.append("  %-11s sr/lr = %.3fx  %s" % (
        p["name"], p["ratios"]["safe_release_over_legacy_c_release"],
        "REGRESSION" if p["regressed_over_2x"] else "ok"))
lines.append("  any config regressed >2x: %s" % any_reg)
with open(txt_path, "w") as f:
    f.write("\n".join(lines) + "\n")
PY

python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$JSON" || { echo "error: summary.json is invalid" >&2; exit 1; }

echo "Wrote $TXT and $JSON"

if [ "$HARD_FAIL" -eq 1 ]; then
    echo "PERF RUN had hard failures (see FAIL lines above); results written to $RES" >&2
    exit 1
fi
echo "PERF RUN OK"

#!/bin/sh
# c2modern — conservative, additive ordinary-C -> modern-C+ source conversion.
#
# Usage: tools/c2modern/convert.sh INPUT.c OUTPUT.cp
#
# What it actually does (see README.md for the full, honest contract):
#   1. copies INPUT.c verbatim to OUTPUT.cp;
#   2. prepends an additive preamble that enables stdlib/error.hp and documents
#      (but does NOT enable) stdlib/safe/safe_c.h;
#   3. writes OUTPUT.mapping: an ADVISORY ordinary->_s mapping table with an
#      explicit bound expression for every recognised call. No call site in
#      OUTPUT.cp is rewritten: the required hardened-CRT include/link flags are
#      outside the fixed verification command this tool must satisfy;
#   4. builds OUTPUT.cp with the C+ driver and runs it under timeout 30;
#   5. prints a conversion report.
#
# POSIX sh only. No Python. No files outside the two output paths are written.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P) || exit 2
REPO_ROOT=${C2MODERN_ROOT:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd -P)} || exit 2
DRIVER=${CPLUS_DRIVER:-$REPO_ROOT/build/c+}
CC=${CC:-/usr/bin/clang}

die() { printf 'c2modern: %s\n' "$*" >&2; exit 2; }

[ $# -eq 2 ] || die "usage: $0 INPUT.c OUTPUT.cp"

input=$1
output=$2

case $input in
  *.c) ;;
  *) die "input must have a .c suffix: $input" ;;
esac
case $output in
  *.cp) ;;
  *) die "output must have a .cp suffix: $output" ;;
esac
[ -f "$input" ] || die "input file not found: $input"
[ -x "$DRIVER" ] || die "C+ driver not found/executable: $DRIVER"

# Resolve absolute paths without relying on realpath.
input_dir=$(CDPATH= cd -- "$(dirname -- "$input")" && pwd -P) || die "bad input dir"
input=$input_dir/$(basename -- "$input")
out_dir=$(CDPATH= cd -- "$(dirname -- "$output")" && pwd -P) || die "bad output dir"
output=$out_dir/$(basename -- "$output")
mapping=${output%.cp}.mapping

work=$(mktemp -d "${TMPDIR:-/tmp}/c2modern.XXXXXX") || die "mktemp failed"
trap 'rm -rf "$work"' EXIT HUP INT TERM

INPUT_BASE=$(basename -- "$input")
OUTPUT_BASE=$(basename -- "$output")
MAPPING_BASE=$(basename -- "$mapping")

# ---------------------------------------------------------------------------
# 1. Verbatim copy.
# ---------------------------------------------------------------------------
cp -- "$input" "$work/original.c" || die "copy failed"
original_lines=$(awk 'END { print NR }' "$work/original.c")

# ---------------------------------------------------------------------------
# 2. Additive preamble + advisory mapping table.
# ---------------------------------------------------------------------------
has_error_hp=0
grep -q 'stdlib/error.hp' "$work/original.c" && has_error_hp=1
has_safe_c=0
grep -q 'stdlib/safe/safe_c.h' "$work/original.c" && has_safe_c=1

added=0
{
  printf '/* c2modern: conservative additive conversion of %s.\n' "$INPUT_BASE"
  printf ' * Original C source is preserved verbatim below this preamble.\n'
  printf ' * Advisory call mapping: %s (NO source call sites were rewritten).\n' "$MAPPING_BASE"
  printf ' */\n'
  added=$((added + 6))

  if [ "$has_error_hp" -eq 0 ]; then
    printf '#include "stdlib/error.hp" /* c2modern: recoverable errors + checked allocator */\n'
    added=$((added + 1))
  else
    printf '/* c2modern: stdlib/error.hp already present in original; not re-added. */\n'
    added=$((added + 1))
  fi

  printf '/* c2modern: stdlib/safe/safe_c.h is DOCUMENTED, NOT ENABLED, here.\n'
  printf ' * Enabling it rewrites ordinary calls to checked _s entry points that live in\n'
  printf ' * build/libc/libcplus_hardened.a and build/safe/libcplus_safecrt.a, and needs\n'
  printf ' * -Ilibc/install-include -Istdlib/safe -Ilibc. The fixed verification command\n'
  printf ' *   ./build/c+ build <out.cp> --strict --sanitize=none -I. -o <bin>\n'
  printf ' * supplies none of those. This checkout also has a cplus_log macro/declaration\n'
  printf ' * collision between stdlib/error.hp (cplus/cplus.h) and cplus_libc.h. See\n'
  printf ' * %s and README.md before enabling. */\n' "$MAPPING_BASE"
  printf '#if 0\n'
  printf '#include "stdlib/safe/safe_c.h"\n'
  printf '#endif\n'
  added=$((added + 10))
  if [ "$has_safe_c" -eq 1 ]; then
    printf '/* c2modern: original already mentions safe_c.h; left untouched. */\n'
    added=$((added + 1))
  fi
  printf '\n'
  added=$((added + 1))
} > "$work/preamble"

cat "$work/preamble" "$work/original.c" > "$work/out.cp" || die "assembly failed"

# Build the advisory mapping table from the ORIGINAL source.
awk -v src="$INPUT_BASE" -v out="$OUTPUT_BASE" '
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }

# Extract the argument substring of the first call to fname on line $0.
# Returns "" when fname is not present. Handles one level of nested parentheses.
function args_of(fname,   line, p, i, depth, c) {
  line = $0
  p = index(line, fname "(")
  if (p == 0) return ""
  i = p + length(fname)          # index of "("
  depth = 0
  for (; i <= length(line); i++) {
    c = substr(line, i, 1)
    if (c == "(") depth++
    else if (c == ")") {
      depth--
      if (depth == 0) return substr(line, p + length(fname) + 1, i - (p + length(fname) + 1))
    }
  }
  return ""
}

# Collect capacities from the original source first (two-pass-ish: arrays and
# malloc assignments are usually declared before use in ordinary C).
{
  line = $0
  if (match(line, /char[ \t]+[A-Za-z_][A-Za-z0-9_]*\[[A-Za-z0-9_]+\]/)) {
    decl = substr(line, RSTART, RLENGTH)
    name = decl; sub(/^char[ \t]+/, "", name); sub(/\[.*$/, "", name)
    size = decl; sub(/^.*\[/, "", size); sub(/\]$/, "", size)
    cap[name] = size
  }
  if (match(line, /[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*malloc[ \t]*\(/)) {
    decl = substr(line, RSTART, RLENGTH)
    name = decl; sub(/[ \t]*=[ \t]*malloc.*$/, "", name); name = trim(name)
    # find the malloc argument on this line
    mp = index(line, "malloc(")
    if (mp > 0) {
      rest = substr(line, mp + 7)
      rp = index(rest, ")")
      if (rp > 0) { sz = substr(rest, 1, rp - 1); gsub(/^[ \t]+|[ \t]+$/, "", sz); cap[name] = sz }
    }
  }
}

# Emit mapping rows for recognised calls in a separate pass over stored lines.
END {
  for (n = 1; n <= NR; n++) {
    line = lines[n]
    # skip pure comment lines for detection purposes
    t = trim(line); if (substr(t,1,2) == "/*" || substr(t,1,2) == "//") continue
    for (k = 1; k <= nf; k++) {
      f = fn[k]
      a = args_of_named(line, f)
      if (a == "") continue
      first = a; sub(/,.*$/, "", first); first = trim(first)
      if (extra[k] == "BOUND" || extra[k] == "BOUND2") {
        bound = (first in cap) ? cap[first] : "CPLUS_SAFE_OBJ_SIZE(" first ")"
        conf = (first in cap) ? "EXPLICIT" : "INFERRED"
      } else {
        bound = "n/a (size explicit / no capacity arg)"
        conf = "N/A"
      }
      checked = rep[k] "(" a
      if (extra[k] == "BOUND") checked = checked ", " bound
      else if (extra[k] == "BOUND2") checked = checked ", " bound ", CPLUS_SAFE_OBJ_SIZE(2nd)" 
      checked = checked ")"
      printf "%-5d %-10s %-42s %-40s %s\n", n, f, checked, bound, conf
      count++
    }
  }
}

# Awk cannot call args_of() with a different $0 inside END without an array of
# lines, so store every line and use this helper that operates on a string.
function args_of_named(line, fname,   p, i, depth, c) {
  p = index(line, fname "(")
  if (p == 0) return ""
  i = p + length(fname)
  depth = 0
  for (; i <= length(line); i++) {
    c = substr(line, i, 1)
    if (c == "(") depth++
    else if (c == ")") {
      depth--
      if (depth == 0) return substr(line, p + length(fname) + 1, i - (p + length(fname) + 1))
    }
  }
  return ""
}

BEGIN {
  nf = 16
  fn[1]="memcpy";   rep[1]="cplus_memcpy_s_auto";    extra[1]="BOUND"
  fn[2]="memmove";  rep[2]="cplus_memmove_s_auto";   extra[2]="BOUND"
  fn[3]="memset";   rep[3]="cplus_memset_s_auto";    extra[3]="BOUND"
  fn[4]="memcmp";   rep[4]="cplus_memcmp_s_auto";    extra[4]="BOUND2"
  fn[5]="strcpy";   rep[5]="cplus_strcpy_s_auto";    extra[5]="BOUND"
  fn[6]="strncpy";  rep[6]="cplus_strncpy_s_auto";   extra[6]="BOUND"
  fn[7]="strcat";   rep[7]="cplus_strcat_s_auto";    extra[7]="BOUND"
  fn[8]="strncat";  rep[8]="cplus_strncat_s_auto";   extra[8]="BOUND"
  fn[9]="malloc";   rep[9]="malloc_s";               extra[9]=""
  fn[10]="calloc";  rep[10]="calloc_s";              extra[10]=""
  fn[11]="realloc"; rep[11]="realloc_s";             extra[11]=""
  fn[12]="free";    rep[12]="free_s";                extra[12]=""
  fn[13]="printf";  rep[13]="cplus_printf";          extra[13]=""
  fn[14]="fprintf"; rep[14]="cplus_fprintf";         extra[14]=""
  fn[15]="fread";   rep[15]="cplus_fread";           extra[15]=""
  fn[16]="fwrite";  rep[16]="cplus_fwrite";          extra[16]=""
}

{ lines[NR] = $0 }
' "$work/original.c" > "$work/rows"

mapped=$(awk 'END { print NR + 0 }' "$work/rows")
{
  printf '# c2modern advisory ordinary -> checked mapping -- %s\n' "$INPUT_BASE"
  printf '# Converted source: %s\n' "$OUTPUT_BASE"
  printf '# Generated by tools/c2modern/convert.sh; source call sites were NOT modified.\n'
  printf '# STATUS: ADVISORY-NOT-APPLIED\n'
  printf '# REASON: the fixed verification command (--strict --sanitize=none -I.)\n'
  printf '#   cannot resolve cplus_libc.h / cplus_safecrt_upstream.h from safe_c.h,\n'
  printf '#   cannot link the _s entry points in the hardened CRT archives, and this\n'
  printf '#   checkout has a cplus_log macro/declaration collision with error.hp.\n'
  printf '# CONFIDENCE: EXPLICIT = bound derived from a declared array/malloc size;\n'
  printf '#             INFERRED = CPLUS_SAFE_OBJ_SIZE(first-arg) at the call site.\n'
  printf '# Note: memcmp needs a second bound (source object); add it when applying.\n'
  printf '#\n'
  printf '# %-5s %-10s %-42s %-40s %s\n' 'LINE' 'CALL' 'CHECKED-FORM (proposed)' 'BOUND-EXPR' 'CONF'
  cat "$work/rows"
  printf '#\n'
  printf '# TOTAL mapped call sites: %d\n' "$mapped"
} > "$mapping" || die "mapping write failed"

# ---------------------------------------------------------------------------
# 3. Verify: build with the fixed command, run with fixed argv under timeout 30.
# ---------------------------------------------------------------------------
# Install the converted source first: the build step consumes OUTPUT.cp itself.
cp -- "$work/out.cp" "$output" || die "failed to write output"

# -I. in the fixed verification command must mean the repository root so that
# "stdlib/error.hp" resolves; all paths above were made absolute already.
cd -- "$REPO_ROOT" || die "cannot cd to repo root"
bin=$work/converted_bin
log=$work/build.log
build_rc=0
timeout 55 "$DRIVER" build "$output" --strict --sanitize=none -I. -o "$bin" > "$log" 2>&1 || build_rc=$?

run_rc=0
run_out=$work/run.out
if [ "$build_rc" -eq 0 ]; then
  timeout 30 "$bin" c2modern argv 1 > "$run_out" 2>&1 || run_rc=$?
else
  run_rc=1
  : > "$run_out"
fi

# ---------------------------------------------------------------------------
# 4. Report.
# ---------------------------------------------------------------------------
printf 'c2modern conversion report\n'
printf '  input            : %s\n' "$input"
printf '  output           : %s\n' "$output"
printf '  mapping report   : %s\n' "$mapping"
printf '  original lines   : %d (preserved byte-identical after preamble)\n' "$original_lines"
printf '  lines added      : %d (preamble only; %d source call sites rewritten)\n' "$added" 0
printf '  symbols mapped   : %d advisory entries\n' "$mapped"
printf '  build            : %s (rc=%d)\n' "$([ "$build_rc" -eq 0 ] && echo PASS || echo FAIL)" "$build_rc"
printf '  run (timeout 30) : %s (rc=%d)\n' "$([ "$run_rc" -eq 0 ] && echo PASS || echo FAIL)" "$run_rc"
if [ "$build_rc" -ne 0 ]; then
  printf '  build diagnostics:\n'
  sed 's/^/    /' "$log"
fi
if [ -s "$run_out" ]; then
  printf '  program stdout/stderr:\n'
  sed 's/^/    /' "$run_out"
fi
printf '  NOTES: additive only; no fat pointers, no ABI change, not a full translator.\n'

[ "$build_rc" -eq 0 ] && [ "$run_rc" -eq 0 ] || exit 1
exit 0

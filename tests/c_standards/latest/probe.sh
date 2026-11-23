#!/bin/sh
# tests/c_standards/latest/probe.sh
#
# Probe EXACTLY what the installed compiler accepts for -std=<value>.
# No assumptions about "C21", "C23", "C2y" or "C26" naming: only the
# compiler's own exit code and diagnostics are recorded.
#
# Artifacts (all inside this directory):
#   .work/probe/clang_version.txt
#   .work/probe/accept.tsv              value<TAB>yes|no<TAB>first diagnostic
#   .work/probe/<value>.err             raw clang stderr/stdout, verbatim
#   .work/probe/<value>.macros          __STDC_VERSION__ for accepted values

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK="$HERE/.work/probe"
mkdir -p "$WORK"

CLANG=${CLANG_BIN:-/usr/bin/clang}
"$CLANG" --version > "$WORK/clang_version.txt" 2>&1

# Required spellings plus a few extra "draft flag" guesses that are often
# assumed to exist.  Order is preserved in accept.tsv.
VALUES="c2x gnu2x c23 gnu23 c2y c26 iso9899:2024 c2b c2y_draft gnu2y"

: > "$WORK/accept.tsv"

printf '%s\n' "Compiler: $(head -1 "$WORK/clang_version.txt")"
printf '%s\n' ""
printf '%-14s %-10s %s\n' 'VALUE' 'ACCEPTED' 'FIRST DIAGNOSTIC (exact)'
printf '%-14s %-10s %s\n' '--------------' '----------' '-----------------------------'

for v in $VALUES; do
  bin="$WORK/probe_${v}.bin"
  rm -f "$bin"
  # Compile from stdin so no scratch source file is created outside this dir.
  printf 'int main(void){return 0;}\n' |
    "$CLANG" -std="$v" -x c - -o "$bin" > "$WORK/$v.err" 2>&1
  rc=$?
  if [ "$rc" -eq 0 ] && [ -x "$bin" ]; then
    acc=yes
    first='(none - compiled cleanly)'
    "$CLANG" -std="$v" -x c -dM -E /dev/null 2>/dev/null \
      | grep -E '__STDC_VERSION__' > "$WORK/$v.macros" 2>/dev/null
  else
    acc=no
    first=$(head -1 "$WORK/$v.err")
  fi
  printf '%s\t%s\t%s\n' "$v" "$acc" "$first" >> "$WORK/accept.tsv"
  printf '%-14s %-10s %s\n' "$v" "$acc" "$first"
  rm -f "$bin"
done

printf '%s\n' ""
printf '%s\n' "Exact diagnostics: $WORK/<value>.err"
printf '%s\n' "Machine-readable result: $WORK/accept.tsv"

#!/usr/bin/env bash
# Reproducible, READ-ONLY verification for the claim:
#   "The original standard C implementation sources are unchanged; the safety
#    integration is additive only."
#
# This script never edits sources and never touches the git index.
# Override the baseline with BASELINE=<rev> and the tip with HEAD_REV=<rev>.
#
# Baseline default: cbcea57 -- merge commit that is the direct parent of the
# first commit of the safety-integration series (960c997). It is also the
# baseline used by the parallel header audit in docs/abi/source_audit.md.

set -eu

BASELINE="${BASELINE:-cbcea57}"
HEAD_REV="${HEAD_REV:-HEAD}"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CORE_FILES=(
  libc/src/string/memory.c
  libc/src/string/string.c
  libc/src/string/extra.c
  libc/src/string/bounded.c
  libc/src/stdlib/alloc.c
  libc/src/stdio/stdio.c
  runtime/cplus_runtime.c
)

fail=0
section() { printf '\n=== %s ===\n' "$1"; }

section "Identities"
base_full="$(git rev-parse "$BASELINE")"
head_full="$(git rev-parse "$HEAD_REV")"
printf 'BASELINE          %s (%s)\n' "$base_full" "$BASELINE"
printf 'HEAD              %s (%s)\n' "$head_full" "$HEAD_REV"
if ! git merge-base --is-ancestor "$BASELINE" "$HEAD_REV"; then
  echo "ERROR: baseline is not an ancestor of HEAD_REV"
  exit 2
fi
printf 'commits in range  %s\n' "$(git rev-list --count "$BASELINE..$HEAD_REV")"

section "Commit range ($BASELINE..$HEAD_REV)"
git log --oneline "$BASELINE..$HEAD_REV"

section "Core files: git blob hash at baseline vs at HEAD"
# Identical blob object ids => byte-for-byte identical file contents.
for f in "${CORE_FILES[@]}"; do
  if ! git cat-file -e "$BASELINE:$f" 2>/dev/null || ! git cat-file -e "$HEAD_REV:$f" 2>/dev/null; then
    printf 'ABSENT    %-38s (missing at baseline and/or HEAD)\n' "$f"
    fail=1
    continue
  fi
  b="$(git rev-parse "$BASELINE:$f")"
  h="$(git rev-parse "$HEAD_REV:$f")"
  if [ "$b" = "$h" ]; then
    printf 'UNCHANGED %-38s %s\n' "$f" "$h"
  else
    printf 'MODIFIED  %-38s baseline=%s head=%s\n' "$f" "$b" "$h"
    fail=1
  fi
done

section "Core files: literal diff ($BASELINE..$HEAD_REV); empty == byte-identical"
for f in "${CORE_FILES[@]}"; do
  if git diff --quiet "$BASELINE..$HEAD_REV" -- "$f"; then
    printf 'EMPTY DIFF  %s\n' "$f"
  else
    printf 'NON-EMPTY   %s\n' "$f"
    git diff --stat "$BASELINE..$HEAD_REV" -- "$f"
    fail=1
  fi
done

section "Core files: uncommitted working-tree changes"
wtc="$(git diff --name-only -- "${CORE_FILES[@]}")"
if [ -z "$wtc" ]; then
  echo "NONE — no uncommitted changes to any core file"
else
  echo "$wtc"
  fail=1
fi

section "libc/src/ change categories ($BASELINE..$HEAD_REV)"
src_ns="$(git diff --name-status "$BASELINE..$HEAD_REV" -- libc/src/)"
printf '%s\n' "$src_ns" | cut -f1 | sort | uniq -c
src_mdr="$(printf '%s\n' "$src_ns" | awk -F'\t' '$1 ~ /^[MDR]/ {print}')"
if [ -z "$src_mdr" ]; then
  echo "No MODIFIED / DELETED / RENAMED entries in libc/src/ — all changes are ADDED."
else
  echo "NON-ADDITIVE entries in libc/src/:"
  printf '%s\n' "$src_mdr"
  fail=1
fi

section "libc/install-include/ change categories ($BASELINE..$HEAD_REV)"
hdr_ns="$(git diff --name-status "$BASELINE..$HEAD_REV" -- libc/install-include/)"
printf '%s\n' "$hdr_ns" | cut -f1 | sort | uniq -c
hdr_mdr="$(printf '%s\n' "$hdr_ns" | awk -F'\t' '$1 ~ /^[MDR]/ {print}')"
if [ -z "$hdr_mdr" ]; then
  echo "No MODIFIED / DELETED / RENAMED public headers — no existing declaration signature could have changed."
else
  echo "NON-ADDITIVE entries in libc/install-include/:"
  printf '%s\n' "$hdr_mdr"
  fail=1
fi

section "Full pre-baseline history of core files (informational, no pass/fail effect)"
# Disclosed so that a genuine pre-baseline modification is never hidden.
for f in "${CORE_FILES[@]}"; do
  printf -- '--- %s ---\n' "$f"
  git log --format='%h %ad %s' --date=short -- "$f"
done

section "Conclusion"
if [ "$fail" -eq 0 ]; then
  echo "PASS: within $BASELINE..$HEAD_REV all 7 core C files are byte-identical,"
  echo "      and every libc/src/ and libc/install-include/ change is an ADDITION (A)."
  echo "      Scope note: core files WERE modified in earlier, pre-baseline commits"
  echo "      (listed above); this is not a claim they equal their first-ever versions."
else
  echo "FAIL: a non-additive change or a modified core file was detected."
fi
exit "$fail"

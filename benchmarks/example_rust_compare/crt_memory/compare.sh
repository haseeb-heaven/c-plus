#!/bin/bash
# Compare each checked-CRT memory example against a safe-Rust equivalent.
#
# Toolchains:
#   C   : /usr/bin/clang -std=c99 -Istdlib/safe -Ilibc/install-include
#                       -Ithird_party/safeclib/include
#         + build/safe/libcplus_safecrt.a + build/libc/libcplus_hardened.a
#         (-Istdlib/safe is REQUIRED: safe_alloc.h / safe_memory.h live there.)
#   Rust: rustc -O --edition 2021   (no unsafe, no FFI)
# Each binary runs under `timeout 30`; stdout bytes and exit status are compared.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../../.." && pwd)"
CB="$HERE/cbuild"
RB="$HERE/rbuild"
mkdir -p "$CB" "$RB"

NAMES="malloc_s calloc_s realloc_s free_s memcpy_s memmove_s memset_s memzero_s memcmp_s"
MATCH=0; DIFFERENT=0; BLOCKED=0

note_for() {
  case "$1" in
    memcpy_s) echo "stdout incl. mirrored safeclib 406 handler line; refusal = Err(NoSpace)";;
    memset_s) echo "stdout incl. mirrored safeclib 406 handler line; refusal = Err(NoSpace)";;
    memmove_s) echo "overlap via copy_within; aliasing modelled as slice offsets";;
    memcmp_s) echo "ordering via return i32 (C uses diff out-param)";;
    malloc_s|calloc_s|realloc_s|free_s) echo "alloc failure/double-free via Option/Result";;
    memzero_s) echo "zeroing via fill(0); zero-length = Err";;
    *) echo "behaviour compared";;
  esac
}

for n in $NAMES; do
  cbin="$CB/$n"; rbin="$RB/$n"
  cblog="$CB/$n.build.log"; rblog="$RB/$n.build.log"
  : > "$cblog"; : > "$rblog"

  ( cd "$REPO_ROOT" && /usr/bin/clang -std=c99 \
      -Istdlib/safe -Ilibc/install-include -Ithird_party/safeclib/include \
      "examples/crt/$n.c" \
      build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
      -o "$cbin" ) > "$cblog" 2>&1
  cbuild=$?

  rustc -O --edition 2021 "$HERE/$n.rs" -o "$rbin" > "$rblog" 2>&1
  rbuild=$?

  if [ "$cbuild" -ne 0 ] || [ "$rbuild" -ne 0 ]; then
    echo "EXAMPLE_COMPARE $n BLOCKED build-failed C=$cbuild Rust=$rbuild (logs: $cblog $rblog)"
    BLOCKED=$((BLOCKED+1))
    continue
  fi

  : > "$CB/$n.stdout"; : > "$CB/$n.stderr"
  : > "$RB/$n.stdout"; : > "$RB/$n.stderr"
  : > "$RB/$n.trace.stdout"; : > "$RB/$n.trace.stderr"

  timeout 30 "$cbin" > "$CB/$n.stdout" 2> "$CB/$n.stderr"; cexit=$?
  timeout 30 "$rbin" > "$RB/$n.stdout" 2> "$RB/$n.stderr"; rexit=$?
  CPLUS_RUST_TRACE=1 timeout 30 "$rbin" > "$RB/$n.trace.stdout" 2> "$RB/$n.trace.stderr"

  csz=$(wc -c < "$CB/$n.stdout" | tr -d ' ')
  rsz=$(wc -c < "$RB/$n.stdout" | tr -d ' ')
  if cmp -s "$CB/$n.stdout" "$RB/$n.stdout" && [ "$cexit" -eq "$rexit" ]; then
    echo "EXAMPLE_COMPARE $n MATCH stdout=${csz}B exit=$cexit; $(note_for "$n")"
    MATCH=$((MATCH+1))
  else
    echo "EXAMPLE_COMPARE $n DIFFERENT stdout C=${csz}B/R=${rsz}B exit C=$cexit/R=$rexit; $(note_for "$n")"
    DIFFERENT=$((DIFFERENT+1))
  fi
done

echo "EXAMPLE_COMPARE TOTAL MATCH=$MATCH DIFFERENT=$DIFFERENT BLOCKED=$BLOCKED"

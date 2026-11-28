#!/bin/sh
# Audit: ordinary C spellings -> checked implementations (macro remapping only).
#
# For each mapped ordinary name this script compiles the SAME tiny C fixture
# twice: plain (host CRT, no force-include) and checked
# (-include stdlib/safe/safe_c.h + the two shipped archives). It runs both on
# valid input and diffs stdout byte-for-byte; then it runs a hostile input
# (null / one-over / oversize / unowned pointer) and requires the checked build
# to refuse (exit 70 or an explicit REFUSED marker). The plain build may crash,
# corrupt memory or even refuse too -- that is the SAFETY delta, not an ABI
# change. Every compile and run is wrapped in `timeout 30`.
#
# It also checks: nm symbol provenance, per-TU isolation (no interposition),
# precompiled-library isolation, CPLUS_SAFE_C_DISABLE declarations-only mode,
# and the names that are deliberately NOT mapped.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

OUT="$HERE/.build"
mkdir -p "$OUT" || exit 1
RESULTS="$HERE/RESULTS.md"
FIX="$HERE/fixture.c"

SAFE="$ROOT/build/safe/libcplus_safecrt.a"
LIBC="$ROOT/build/libc/libcplus_hardened.a"
INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBS="$SAFE $LIBC -lm -pthread"
NM=${NM:-nm}

if [ -n "${CPLUS_CLANG:-}" ]; then CC=$CPLUS_CLANG
elif [ -x /usr/bin/clang ]; then CC=/usr/bin/clang
else CC=clang; fi

for a in "$SAFE" "$LIBC"; do
  if [ ! -f "$a" ]; then
    echo "MISSING PREREQUISITE: $a"
    exit 1
  fi
done

echo "CC=$CC"
echo "SAFE=$SAFE"
echo "LIBC=$LIBC"

NAMES="malloc calloc realloc free memset memcpy memmove memcmp strcpy strncpy strcat strncat fread fwrite printf fprintf"
ROWS="$OUT/rows.txt"
: > "$ROWS"
pass=0; fail=0; mapped_fail=0

for n in $NAMES; do
  u=$(printf '%s' "$n" | tr '[:lower:]' '[:upper:]')
  def="-DF_$u"

  timeout 30 "$CC" -O1 -std=c11 $INC $def "$FIX" -o "$OUT/$n.plain" \
      >"$OUT/$n.plain.build.log" 2>&1
  pbuild=$?
  timeout 30 "$CC" -O1 -std=c11 $INC -include stdlib/safe/safe_c.h $def "$FIX" \
      $LIBS -o "$OUT/$n.checked" >"$OUT/$n.checked.build.log" 2>&1
  cbuild=$?

  status=PASS
  [ "$pbuild" -eq 0 ] || status=FAIL
  [ "$cbuild" -eq 0 ] || status=FAIL

  valid="n/a"
  hostile_plain="n/a"
  hostile_checked="n/a"
  plain_out="n/a"

  if [ "$status" = PASS ]; then
    timeout 30 "$OUT/$n.plain" valid >"$OUT/$n.plain.valid.out" 2>"$OUT/$n.plain.valid.err"
    pv=$?
    timeout 30 "$OUT/$n.checked" valid >"$OUT/$n.checked.valid.out" 2>"$OUT/$n.checked.valid.err"
    cv=$?
    if [ "$pv" -eq 0 ] && [ "$cv" -eq 0 ] && cmp -s "$OUT/$n.plain.valid.out" "$OUT/$n.checked.valid.out"; then
      valid="identical (rc $pv/$cv)"
      plain_out=$(cat "$OUT/$n.checked.valid.out")
    else
      valid="DIFFER (rc $pv/$cv)"
      status=FAIL
    fi

    timeout 30 "$OUT/$n.checked" hostile >"$OUT/$n.checked.hostile.out" 2>"$OUT/$n.checked.hostile.err"
    ch=$?
    if [ "$ch" -eq 70 ] || grep -q '^REFUSED$' "$OUT/$n.checked.hostile.out"; then
      hostile_checked="REFUSED (rc $ch)"
    else
      hostile_checked="NO REFUSAL (rc $ch)"
      status=FAIL
    fi

    # The plain build is informational only: a crash/corruption is the expected
    # safety delta, and a refusal for some inputs (e.g. oversize malloc) is fine.
    ( timeout 30 "$OUT/$n.plain" hostile >"$OUT/$n.plain.hostile.out" 2>"$OUT/$n.plain.hostile.err" ) 2>/dev/null
    ph=$?
    hostile_plain="rc $ph, stdout=$(cat "$OUT/$n.plain.hostile.out" 2>/dev/null)"
  fi

  printf '%s|%s|%s|%s|%s|%s|%s\n' \
      "$n" "$status" "$valid" "$hostile_checked" "$hostile_plain" "$plain_out" \
      "$pbuild/$cbuild" >> "$ROWS"

  if [ "$status" = PASS ]; then
    pass=$((pass + 1))
    echo "ok   $n: valid $valid; hostile checked $hostile_checked; plain $hostile_plain"
  else
    fail=$((fail + 1))
    mapped_fail=$((mapped_fail + 1))
    echo "FAIL $n: build(plain/checked)=$pbuild/$cbuild valid=$valid hostile=$hostile_checked"
    tail -6 "$OUT/$n.plain.build.log" 2>/dev/null
    tail -6 "$OUT/$n.checked.build.log" 2>/dev/null
  fi
done

# ---- nm provenance ---------------------------------------------------------
NMFILE="$OUT/nm_audit.txt"
: > "$NMFILE"
nm_check() {
  sym=$1; arch=$2
  line=$($NM -g "$arch" 2>/dev/null | awk -v s="_$sym" \
      '$3 == s && ($2 == "T" || $2 == "D" || $2 == "B") { print; exit }')
  if [ -n "$line" ]; then
    printf '| `%s` | `%s` | `%s` | PRESENT |\n' "$sym" "$(basename "$arch")" "$line" >> "$NMFILE"
    echo "nm ok: $sym in $(basename "$arch")"
  else
    printf '| `%s` | `%s` | (none) | **MISSING** |\n' "$sym" "$(basename "$arch")" >> "$NMFILE"
    echo "nm MISSING: $sym in $(basename "$arch")"
    fail=$((fail + 1))
  fi
}

for s in malloc_s calloc_s realloc_s free_s \
         cplus_memset_s_auto cplus_memcpy_s_auto cplus_memmove_s_auto cplus_memcmp_s_auto \
         cplus_strcpy_s_auto cplus_strncpy_s_auto cplus_strcat_s_auto cplus_strncat_s_auto; do
  nm_check "$s" "$SAFE"
done
for s in cplus_fread cplus_fwrite cplus_printf cplus_fprintf \
         cplus_sc_upstream_memrchr_s cplus_sc_upstream_bsearch_s \
         cplus_malloc cplus_calloc cplus_realloc cplus_free \
         cplus_memset_bounded cplus_memcpy_bounded cplus_memmove_bounded cplus_memcmp_bounded \
         cplus_strcpy_bounded cplus_strncpy_bounded cplus_strcat_bounded cplus_strncat_bounded; do
  nm_check "$s" "$LIBC"
done

# ---- per-TU isolation / no interposition / precompiled library -------------
ISO="$OUT/iso.txt"
: > "$ISO"
timeout 30 "$CC" -O1 -std=c11 $INC -c "$HERE/tu_plain.c" -o "$OUT/tu_plain.o" 2>"$OUT/iso.plain.build.log"
ip=$?
timeout 30 "$CC" -O1 -std=c11 $INC -include stdlib/safe/safe_c.h -c "$HERE/tu_checked.c" -o "$OUT/tu_checked.o" 2>"$OUT/iso.checked.build.log"
ic=$?
timeout 30 "$CC" -O1 -std=c11 $INC -DCPLUS_SAFE_C_DISABLE -include stdlib/safe/safe_c.h \
    -c "$HERE/tu_checked.c" -o "$OUT/tu_disabled.o" 2>"$OUT/iso.disabled.build.log"
id=$?
timeout 30 "$CC" -O1 -std=c11 $INC -include stdlib/safe/safe_c.h \
    -c "$HERE/unmapped_probe.c" -o "$OUT/unmapped.o" 2>"$OUT/iso.unmapped.build.log"
iu=$?

syms() { $NM -u "$1" 2>/dev/null | awk '{print $NF}' | sort -u; }
check_undef() {
  label=$1; file=$2; expected=$3; forbidden=$4
  s=$(syms "$file")
  has_yes=$(printf '%s\n' "$s" | grep -cx -- "$expected" || true)
  has_no=$(printf '%s\n' "$s" | grep -cx -- "$forbidden" || true)
  if [ "$has_yes" -ge 1 ] && [ "$has_no" -eq 0 ]; then
    printf '| `%s` | `%s` | `%s` | `%s` | PASS |\n' "$label" "$(basename "$file")" "$expected" "$forbidden" >> "$ISO"
    echo "iso ok: $label -> $expected"
  else
    printf '| `%s` | `%s` | `%s` | `%s` | **FAIL** |\n' "$label" "$(basename "$file")" "$expected" "$forbidden" >> "$ISO"
    echo "iso FAIL: $label (expected=$expected forbidden=$forbidden)"
    fail=$((fail + 1))
  fi
}

if [ "$ip" -eq 0 ] && [ "$ic" -eq 0 ] && [ "$id" -eq 0 ] && [ "$iu" -eq 0 ]; then
  check_undef 'TU without header (ordinary malloc)' "$OUT/tu_plain.o" _malloc _malloc_s
  check_undef 'TU with header (ordinary malloc)' "$OUT/tu_checked.o" _malloc_s _malloc
  check_undef 'CPLUS_SAFE_C_DISABLE (declarations only)' "$OUT/tu_disabled.o" _malloc _malloc_s

  timeout 30 ar rcs "$OUT/libprecomp.a" "$OUT/tu_plain.o" 2>/dev/null
  check_undef 'precompiled static library (no header)' "$OUT/libprecomp.a" _malloc _malloc_s
  check_undef 'ordinary fopen (not mapped)' "$OUT/unmapped.o" _fopen _cplus_fopen_s
  check_undef 'ordinary fclose (not mapped)' "$OUT/unmapped.o" _fclose _cplus_fclose_s
  check_undef 'ordinary strlen (not mapped)' "$OUT/unmapped.o" _strlen _cplus_strlen
  check_undef 'ordinary strnlen (not mapped)' "$OUT/unmapped.o" _strnlen _cplus_strnlen_s

  timeout 30 "$CC" -O1 -std=c11 $INC "$HERE/driver.c" "$OUT/tu_plain.o" "$OUT/tu_checked.o" \
      $LIBS -o "$OUT/tu_driver" >"$OUT/iso.link.log" 2>&1
  dl=$?
  dout=$(timeout 30 "$OUT/tu_driver" 2>/dev/null)
  drc=$?
  if [ "$dl" -eq 0 ] && [ "$drc" -eq 0 ] && [ "$dout" = "tu_isolation:ok" ]; then
    printf '| linked plain+checked TUs in one binary | `%s` | run output | `%s` | PASS |\n' \
        "$(basename "$OUT/tu_driver")" "$dout" >> "$ISO"
    echo "iso ok: linked both TUs: $dout"
  else
    printf '| linked plain+checked TUs in one binary | driver | rc %s | `%s` | **FAIL** |\n' \
        "$drc" "$dout" >> "$ISO"
    echo "iso FAIL: driver rc=$drc out=$dout"
    fail=$((fail + 1))
  fi
else
  echo "iso SKIPPED: object compile failed ($ip/$ic/$id/$iu)"
  fail=$((fail + 1))
fi

# ---- RESULTS.md ------------------------------------------------------------
{
  printf '# stdc_mapping verification results\n\n'
  printf 'Generated by `tests/abi/stdc_mapping/verify.sh` from the repository root.\n\n'
  printf 'Compiler: `%s` (`%s`).\n' "$CC" "$($CC --version 2>/dev/null | head -1)"
  printf 'Archives: `%s`, `%s`.\n' "$SAFE" "$LIBC"
  printf 'Compile flags (plain): `-O1 -std=c11 %s`.\n' "$INC"
  printf 'Compile flags (checked): same plus `-include stdlib/safe/safe_c.h`, with `%s`.\n\n' "$LIBS"

  printf '## 1. Per-name mapping audit\n\n'
  printf '| Ordinary name | Result | Valid stdout (plain vs checked) | Hostile checked | Hostile plain (informational) | Valid output | Build rc p/c |\n'
  printf '|---|---|---|---|---|---|---|\n'
  while IFS='|' read -r n st va hc hp po br; do
    printf '| `%s` | %s | %s | %s | %s | `%s` | `%s` |\n' "$n" "$st" "$va" "$hc" "$hp" "$po" "$br"
  done < "$ROWS"
  printf '\n'

  printf '## 2. nm provenance of checked targets\n\n'
  printf 'Every target below is a **defined** symbol (`T`/`D`/`B`) in the shipped archive; underscore shown as nm reports it on Mach-O.\n\n'
  printf '| Target symbol | Archive | nm line | Status |\n|---|---|---|---|\n'
  cat "$NMFILE"
  printf '\n'

  printf '## 3. Per-TU isolation / no interposition / precompiled libraries\n\n'
  printf '`nm -u` undefined-symbol checks. "forbidden" absent proves the ordinary\n'
  printf 'spelling in that TU is not redirected by a global symbol override.\n\n'
  printf '| Probe | Object | Expected undef | Forbidden undef | Status |\n|---|---|---|---|---|\n'
  cat "$ISO"
  printf '\n'

  printf '## 4. Summary\n\n'
  printf 'Mapped-name checks: **%d passed, %d failed**. Total failures including nm/TU-isolation probes: **%d**.\n\n' "$pass" "$mapped_fail" "$fail"
  printf 'Mechanism: the rewrite happens in the **including translation unit only**\n'
  printf '(preprocessor `#undef`/`#define`). There is **no symbol interposition**:\n'
  printf 'a TU compiled without the header still relocates `malloc` to the host CRT,\n'
  printf 'and precompiled C libraries are unaffected. `-DCPLUS_SAFE_C_DISABLE`\n'
  printf 'suppresses every mapping and yields declarations only.\n\n'
  printf 'The hostile-input delta is a **safety** difference (refusal vs memory\n'
  printf 'corruption), not an ABI difference: no struct layout, calling convention,\n'
  printf 'or exported C symbol changes.\n'
} > "$RESULTS"

echo "RESULTS: $RESULTS"
echo "SUMMARY: mapped-name pass=$pass fail=$mapped_fail; total failures(incl. nm/iso)=$fail"
[ "$fail" -eq 0 ]

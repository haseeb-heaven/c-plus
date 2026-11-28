#!/bin/sh
# Validate the compiler-visible libc contract schema.
# Shell-only equivalent of the former tests/core/validate_contracts.py.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P=$ROOT/compiler/Analysis/libc_contracts.json
[ -f "$P" ] || { echo "missing contracts file: $P"; exit 1; }

awk '
BEGIN {
  nreq = split("calloc fclose fopen fread free free_s fwrite malloc malloc_s memcpy memcpy_s memmove memset realloc strcat strcpy strcpy_s strlen strncpy", req, " ")
  nkeys = split("params ownership effects diagnostics failure paramTypes pointerBounds allocationSize ownershipTransfer borrowLifetime readEffects writeEffects freeEffects returnState threadAssumptions", keys, " ")
  inside = 0; n = 0
}
/^  "functions": \{/ { inside = 1; next }
inside && /^  \}/ { inside = 0; next }
inside && /^    "[^"]+": \{/ {
  name = $0
  sub(/^    "/, "", name); sub(/": \{$/, "", name)
  seen[name] = 1; order[++n] = name; cur = name
  next
}
inside && /^      "[^"]+":/ {
  key = $0
  sub(/^      "/, "", key); sub(/":.*$/, "", key)
  got[cur SUBSEP key] = 1
  if (key == "diagnostics") {
    d = $0; sub(/^[^:]*: "/, "", d); sub(/",?$/, "", d); diag[cur] = d
  }
  next
}
END {
  miss = ""
  for (i = 1; i <= nreq; i++)
    if (!(req[i] in seen)) miss = miss (miss == "" ? "" : ", ") req[i]
  if (miss != "") { print "missing contracts: " miss; exit 1 }
  for (i = 1; i <= n; i++) {
    name = order[i]
    for (j = 1; j <= nkeys; j++)
      if (!((name SUBSEP keys[j]) in got)) { print name ": missing " keys[j]; exit 1 }
    if (index(diag[name], "CPLUS-SAFE-") != 1) {
      print name ": bad diagnostic category"; exit 1
    }
  }
  print "contract schema: ok (" n " functions)"
}
' "$P"

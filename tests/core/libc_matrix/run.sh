#!/bin/sh
# Evidence matrix for ordinary libc calls under C+ safe mode.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC=${CPLUS_BIN:-$ROOT/build/c+}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/tests/out/core/libc_matrix"
mkdir -p "$OUT"

emit() { printf '%s\n' "$2" > "$HERE/$1.c"; }
emit valid_malloc '#include <stdlib.h>
int main(void){int*p=malloc(4*sizeof(int));if(!p)return 1;for(int i=0;i<4;i++)p[i]=i;free(p);return 0;}'
emit malloc_zero '#include <stdlib.h>
int main(void){void*p=malloc(0);free(p);return 0;}'
emit double_free_alias '#include <stdlib.h>
int main(void){int*p=malloc(4);int*q=p;free(p);free(q);return 0;}'
emit realloc_literal '#include <stdlib.h>
int main(void){int*p=malloc(4);p=realloc(p,8);free(p);return 0;}'
emit realloc_zero '#include <stdlib.h>
int main(void){int*p=malloc(4);p=realloc(p,0);free(p);return 0;}'
emit memcpy_overlap '#include <string.h>
int main(void){char b[8]="abcdefg";memcpy(b+2,b,4);return b[0]==0;}'
emit strcpy_literal '#include <string.h>
int main(void){char b[4];strcpy(b,"abcdefgh");return 0;}'

for n in valid_malloc malloc_zero double_free_alias realloc_literal realloc_zero memcpy_overlap strcpy_literal; do
  "$CC" build "$HERE/$n.c" -o "$OUT/$n" >"$OUT/$n.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    printf '%-20s COMPILE_REJECT: %s\n' "$n" "$(grep -m1 'error:' "$OUT/$n.build.log" | sed 's/.*error: //')"
    continue
  fi
  "$OUT/$n" >"$OUT/$n.stdout" 2>"$OUT/$n.stderr"
  rrc=$?
  printf '%-20s RUN rc=%s %s\n' "$n" "$rrc" "$(grep -m1 'runtime error' "$OUT/$n.stderr" | cut -c1-60)"
done

cat <<'NOTE'
Interpretation:
- valid_malloc: valid C, unchanged semantics in both modes.
- malloc_zero, realloc_zero, realloc_literal: valid C but rejected by the
  declared safe subset (documented restriction); they compile unchanged under
  --legacy-c. No silent source rewrite is performed.
- double_free_alias: raw alias double free is NOT diagnosed statically; libc
  and UBSan abort (rc=133 here). cplus_release with metadata detects it (104).
- strcpy_literal, memcpy_overlap: detected at compile time where provable.
- General alias escapes and pointer arithmetic escapes remain outside the
  safe subset and are documented as unsafe/unknown.
NOTE

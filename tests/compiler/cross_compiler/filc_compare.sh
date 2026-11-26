#!/bin/bash
# Run the cross-compiler safety scenarios with the real Fil-C compiler.
# Executed inside a Linux aarch64 container; /repo is the mounted repository
# and /work holds the extracted Fil-C release.
set -u
CLANG=/work/filc/build/bin/clang
OUT=/repo/tests/compiler/cross_compiler/results/filc_actual
mkdir -p "$OUT"
: > "$OUT/results.tsv"

run_case() {
  name=$1
  src="$OUT/${name}.c"
  exe="$OUT/${name}.bin"
  cat > "$src"
  ccompile=$($CLANG "$src" -o "$exe" 2>&1)
  crc=$?
  if [ $crc -ne 0 ]; then
    printf '%s\tcompile_reject\t%s\t%s\t%s\n' "$name" "$crc" \
      "$(printf '%s' "$ccompile" | tr '\n\t' '  ' | cut -c1-300)" - >> "$OUT/results.tsv"
    return
  fi
  rout=$(timeout 20 "$exe" 2>&1)
  rrc=$?
  detail=$(printf '%s' "$rout" | tr '\n\t' '  ' | cut -c1-300)
  [ -n "$detail" ] || detail='(no output)'
  printf '%s\trun\t%s\t%s\t%s\n' "$name" "$crc" "$rrc" "$detail" >> "$OUT/results.tsv"
}

run_case const_oob <<'EOF'
int main(void){int a[4]={0};return a[7];}
EOF
run_case overflow <<'EOF'
static int add1(int x){return x+1;}
int main(void){return add1(2147483647);}
EOF
run_case null_deref <<'EOF'
static void d(int *p){*p=1;}
int main(void){d(0);return 0;}
EOF
run_case clean <<'EOF'
int main(void){int s=0;for(int i=0;i<100;i++)s+=i;return s==4950?0:1;}
EOF
cat "$OUT/results.tsv"

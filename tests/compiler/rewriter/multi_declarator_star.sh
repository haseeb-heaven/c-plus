#!/bin/sh
# Regression for declarator stars after a comma in a declaration list.
#
# The null-deref rewriter used to treat the `*` in `T *a, *b;` as a
# dereference and emitted `T *a, *CPLUS_NONNULL(b);`, which is not valid C.
# This script builds one probe that mixes multi-declarator declarations with
# genuine dereferences, multiplication and an array subscript, then checks the
# lowered C: declarators must stay declarations, while actual dereferences
# must still carry their runtime null checks.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/multi_declarator_star"
mkdir -p "$OUT"

cat > "$OUT/probe.c" <<'EOF'
typedef struct node { int v; } node_t;

int probe(int *p, int n) {
    node_t local = { 7 };
    node_t *a, *b;               /* typedef'd type, two declarators */
    const char *src, *note;      /* qualified builtin type */
    int *x = p, *y = p;          /* declarators with initializers */
    int arr[4], *q;              /* array then pointer declarator */
    int product = n * 2;         /* multiplication: not a declarator */
    int sum = 0;
    a = &local;
    b = a;
    src = "ok";
    note = "note";
    x = y = p;
    q = p;
    arr[0] = 1;
    sum = *p + product + arr[0] + (a == b ? 1 : 0) + (src[0] == 'o' ? 1 : 0);
    return sum + a->v + q[0] + (note != 0 ? 1 : 0);
}

int main(void) {
    int v = 5;
    return probe(&v, 3) == 27 ? 0 : 1;
}
EOF

fail() {
    echo "multi-declarator star regression: FAIL ($1)" >&2
    exit 1
}

if ! "$CPLUS" build "$OUT/probe.c" --default --sanitize=none \
        --emit-c "$OUT/probe.lowered.c" -o "$OUT/probe.bin" \
        >"$OUT/build.log" 2>&1; then
    cat "$OUT/build.log" >&2
    fail "probe build failed"
fi

LOWERED="$OUT/probe.lowered.c"

# Positive: real dereferences are still instrumented.
grep -qF '*CPLUS_NONNULL(p)' "$LOWERED" || fail "lost check for *p"
grep -qF 'CPLUS_NONNULL(src)[0]' "$LOWERED" || fail "lost check for src[0]"
grep -qF 'CPLUS_NONNULL(a)->v' "$LOWERED" || fail "lost check for a->v"

# Negative: no declarator identifier may be wrapped in CPLUS_NONNULL.
if grep -Eq ',[[:space:]]*\*CPLUS_NONNULL\(' "$LOWERED"; then
    fail "CPLUS_NONNULL injected after a comma"
fi
for name in b note y q; do
    if grep -qF "CPLUS_NONNULL($name)" "$LOWERED"; then
        fail "CPLUS_NONNULL injected into declarator '$name'"
    fi
done
# Multiplication and array subscripts stay expression forms.
if grep -Eq 'CPLUS_NONNULL\((n|product|arr|2)\)' "$LOWERED"; then
    fail "expression operand treated as a pointer base"
fi

"$OUT/probe.bin" || fail "probe exited nonzero"

echo "multi-declarator star regression: PASS"

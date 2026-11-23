"""Deterministic C corpus for the C+ reference-compiler differential harness.

Every fixture is generated from a fixed master seed and a per-case seed with a
small splitmix64 PRNG.  Regenerating the corpus is therefore reproducible byte
for byte, and each mismatch record can carry the exact seed plus source that
produced it.

The fixtures deliberately stay inside the plain C99/C11 subset that C+ lowers
in ``--legacy-c`` mode: no undefined behaviour, no address printing, no
implementation-order-dependent output (qsort keys are distinct, iteration is
explicit, floating point uses only exact small integers divided by powers of
two).  Only stdout/stderr text is observed, so any difference is a real
compiler/runtime difference rather than a nondeterministic address.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from typing import Callable

MASTER_SEED = 0x43504C55532D4431  # ASCII "CPLUS-D1"


def splitmix64(state: int) -> int:
    mask = (1 << 64) - 1
    state = (state + 0x9E3779B97F4A7C15) & mask
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & mask
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & mask
    return z ^ (z >> 31)


class Rng:
    """Tiny deterministic PRNG; public so mismatch records can replay it."""

    def __init__(self, seed: int):
        self.state = seed & ((1 << 64) - 1)

    def next_u64(self) -> int:
        self.state = (self.state + 0x9E3779B97F4A7C15) & ((1 << 64) - 1)
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & ((1 << 64) - 1)
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & ((1 << 64) - 1)
        return z ^ (z >> 31)

    def i32(self, lo: int, hi: int) -> int:
        """Uniform integer in [lo, hi] (inclusive), no modulo bias concerns here."""
        assert lo <= hi
        return lo + self.next_u64() % (hi - lo + 1)


@dataclass(frozen=True)
class Case:
    name: str
    category: str
    seed: int
    source: str

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.source.encode("utf-8")).hexdigest()


_TOKEN = re.compile(r"@([A-Z0-9_]+)@")


def render(template: str, **values: int) -> str:
    def sub(match: "re.Match[str]") -> str:
        key = match.group(1)
        if key not in values:
            raise KeyError("unrendered corpus token @%s@" % key)
        return str(values[key])

    out = _TOKEN.sub(sub, template)
    return out.lstrip("\n")


def _generator(category: str, template: str,
               values: Callable[[Rng], dict]):
    return category, template, values


GENERATORS = {
    # ---------------------------------------------------------------- arithmetic
    "arith_add_sub": _generator(
        "arithmetic",
        r"""
#include <stdio.h>
int main(void) {
    int a = @A@, b = @B@, c = @C@, d = @D@;
    printf("add=%d sub=%d add2=%d sub2=%d\n", a + b, a - b, c + d, c - d);
    printf("neg=%d id=%d\n", -a, b + 0);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 100000), B=r.i32(1, 100000),
                       C=r.i32(1, 100000), D=r.i32(1, 100000)),
    ),
    "arith_mul_div": _generator(
        "arithmetic",
        r"""
#include <stdio.h>
int main(void) {
    unsigned a = @A@u, b = @B@u, c = @C@u, d = @D@u;
    printf("mul=%u div=%u mod=%u\n", a * b, c / d, c % d);
    printf("wrap=%u\n", a * b * c);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 1000), B=r.i32(1, 1000),
                       C=r.i32(1, 100000), D=r.i32(1, 1000)),
    ),
    "arith_bitwise": _generator(
        "arithmetic",
        r"""
#include <stdio.h>
int main(void) {
    unsigned a = @A@u, b = @B@u;
    printf("and=%u or=%u xor=%u\n", a & b, a | b, a ^ b);
    printf("not=%u shl=%u shr=%u\n", ~a, a << @S@, b >> @S@);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 0xFFFF), B=r.i32(1, 0xFFFF), S=r.i32(1, 7)),
    ),
    "arith_mixed_signed": _generator(
        "arithmetic",
        r"""
#include <stdio.h>
int main(void) {
    int s = @S@;
    unsigned u = @U@u;
    long l = @L@L;
    printf("cmp_su=%d cmp_us=%d\n", s < (int)u, u < (unsigned)s);
    printf("cast=%lu sum=%ld\n", (unsigned long)u, (long)s + l);
    return 0;
}
""",
        lambda r: dict(S=r.i32(1, 10000), U=r.i32(1, 10000), L=r.i32(1, 10000)),
    ),
    "arith_float": _generator(
        "arithmetic",
        r"""
#include <stdio.h>
int main(void) {
    double a = @A@ / 8.0;
    double b = @B@ / 16.0;
    printf("add=%.6f sub=%.6f\n", a + b, a - b);
    printf("mul=%.6f div=%.6f\n", a * b, a / b);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 10000), B=r.i32(1, 10000)),
    ),
    "arith_compound": _generator(
        "arithmetic",
        r"""
#include <stdio.h>
int main(void) {
    int x = @A@;
    x += @B@; x -= @C@; x *= 2; x /= 2;
    int y = x++;
    int z = ++x;
    printf("x=%d y=%d z=%d\n", x, y, z);
    printf("mod=%d\n", x % @D@);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 1000), B=r.i32(1, 1000),
                       C=r.i32(1, 100), D=r.i32(2, 100)),
    ),

    # ------------------------------------------------------------------- structs
    "struct_layout": _generator(
        "structs",
        r"""
#include <stdio.h>
#include <stddef.h>
struct Mixed { char a; int b; double c; short d; };
int main(void) {
    printf("size=%zu a=%zu b=%zu c=%zu d=%zu\n", sizeof(struct Mixed),
           offsetof(struct Mixed, a), offsetof(struct Mixed, b),
           offsetof(struct Mixed, c), offsetof(struct Mixed, d));
    return 0;
}
""",
        lambda r: {},
    ),
    "struct_nested": _generator(
        "structs",
        r"""
#include <stdio.h>
struct Inner { int x; int y; };
struct Outer { char tag; struct Inner in; double d; };
int main(void) {
    struct Outer o;
    o.tag = @T@; o.in.x = @X@; o.in.y = @Y@; o.d = @D@ / 4.0;
    struct Outer *p = &o;
    printf("tag=%d x=%d y=%d d=%.6f\n", p->tag, p->in.x, p->in.y, p->d);
    return 0;
}
""",
        lambda r: dict(T=r.i32(1, 127), X=r.i32(1, 10000),
                       Y=r.i32(1, 10000), D=r.i32(1, 10000)),
    ),
    "struct_by_value": _generator(
        "structs",
        r"""
#include <stdio.h>
struct Pair { int a; int b; };
static struct Pair make_pair(int a, int b) {
    struct Pair p; p.a = a; p.b = b; return p;
}
static int pair_sum(struct Pair p) { return p.a + p.b; }
int main(void) {
    struct Pair p = make_pair(@A@, @B@);
    printf("a=%d b=%d sum=%d\n", p.a, p.b, pair_sum(p));
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 10000), B=r.i32(1, 10000)),
    ),
    "union_enum": _generator(
        "structs",
        r"""
#include <stdio.h>
union U { int i; unsigned u; double d; };
enum E { E_ZERO = 0, E_SEVEN = 7, E_NINE = 9 };
int main(void) {
    union U v;
    v.i = @I@;
    printf("i=%d u=%u\n", v.i, v.u);
    v.d = @D@ / 2.0;
    printf("d=%.6f size=%zu\n", v.d, sizeof(union U));
    printf("enum=%d,%d,%d\n", E_ZERO, E_SEVEN, E_NINE);
    return 0;
}
""",
        lambda r: dict(I=r.i32(1, 100000), D=r.i32(1, 10000)),
    ),

    # ------------------------------------------------------------------ pointers
    "ptr_arith": _generator(
        "pointers",
        r"""
#include <stdio.h>
int main(void) {
    int a[5] = {@A@, @B@, @C@, @D@, @E@};
    int *p = a;
    printf("p0=%d p2=%d p4=%d\n", *p, *(p + 2), *(p + 4));
    p++;
    printf("adv=%d diff=%ld\n", *p, (long)(p - a));
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 1000), B=r.i32(1, 1000), C=r.i32(1, 1000),
                       D=r.i32(1, 1000), E=r.i32(1, 1000)),
    ),
    "ptr_ptr": _generator(
        "pointers",
        r"""
#include <stdio.h>
static void swap(int **pp) {
    int *t = pp[0]; pp[0] = pp[1]; pp[1] = t;
}
int main(void) {
    int a = @A@, b = @B@;
    int *pa = &a, *pb = &b;
    int *table[2] = {pa, pb};
    swap(table);
    printf("a=%d b=%d first=%d second=%d\n", a, b, *table[0], *table[1]);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 10000), B=r.i32(1, 10000)),
    ),
    "ptr_const_void": _generator(
        "pointers",
        r"""
#include <stdio.h>
int main(void) {
    int x = @X@;
    const int *cp = &x;
    void *vp = (void *)cp;
    const int *back = (const int *)vp;
    printf("val=%d eq=%d sizes=%zu,%zu\n", *back, cp == back,
           sizeof(cp), sizeof(vp));
    return 0;
}
""",
        lambda r: dict(X=r.i32(1, 100000)),
    ),
    "ptr_array_decay": _generator(
        "pointers",
        r"""
#include <stdio.h>
int main(void) {
    int a[4] = {@A@, @B@, @C@, @D@};
    int *p = a;
    printf("sizeof_arr=%zu sizeof_ptr=%zu\n", sizeof(a), sizeof(p));
    printf("p1=%d p3=%d\n", p[1], p[3]);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 1000), B=r.i32(1, 1000),
                       C=r.i32(1, 1000), D=r.i32(1, 1000)),
    ),

    # -------------------------------------------------------------------- arrays
    "array_1d": _generator(
        "arrays",
        r"""
#include <stdio.h>
int main(void) {
    int a[6] = {@A@, @B@, @C@, @D@, @E@, @F@};
    int sum = 0, mn = a[0], mx = a[0];
    for (int i = 0; i < 6; i++) {
        sum += a[i];
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    printf("sum=%d min=%d max=%d\n", sum, mn, mx);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 100), B=r.i32(1, 100), C=r.i32(1, 100),
                       D=r.i32(1, 100), E=r.i32(1, 100), F=r.i32(1, 100)),
    ),
    "array_2d": _generator(
        "arrays",
        r"""
#include <stdio.h>
int main(void) {
    int m[3][3] = {{@A@,@B@,@C@},{@D@,@E@,@F@},{@G@,@H@,@I@}};
    int trace = 0;
    for (int i = 0; i < 3; i++) trace += m[i][i];
    printf("trace=%d m00=%d m12=%d m21=%d\n", trace, m[0][0], m[1][2], m[2][1]);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 100), B=r.i32(1, 100), C=r.i32(1, 100),
                       D=r.i32(1, 100), E=r.i32(1, 100), F=r.i32(1, 100),
                       G=r.i32(1, 100), H=r.i32(1, 100), I=r.i32(1, 100)),
    ),
    "array_char_string": _generator(
        "arrays",
        r"""
#include <stdio.h>
#include <string.h>
int main(void) {
    char buf[32] = "prefix";
    strcat(buf, "-@TAG@");
    printf("str=%s len=%zu first=%c last=%c\n", buf, strlen(buf), buf[0],
           buf[strlen(buf) - 1]);
    return 0;
}
""",
        lambda r: dict(TAG="tag%d" % r.i32(1, 999999)),
    ),
    "array_struct": _generator(
        "arrays",
        r"""
#include <stdio.h>
struct Item { int key; int val; };
int main(void) {
    struct Item a[4];
    int keys[4] = {@K0@, @K1@, @K2@, @K3@};
    int j;
    for (int i = 0; i < 4; i++) { a[i].key = keys[i]; a[i].val = i + 1; }
    for (int i = 1; i < 4; i++) {
        struct Item t = a[i];
        j = i - 1;
        while (j >= 0 && a[j].key > t.key) { a[j + 1] = a[j]; j--; }
        a[j + 1] = t;
    }
    printf("keys=%d,%d,%d,%d\n", a[0].key, a[1].key, a[2].key, a[3].key);
    printf("vals=%d,%d,%d,%d\n", a[0].val, a[1].val, a[2].val, a[3].val);
    return 0;
}
""",
        lambda r: dict(K0=r.i32(1, 1000), K1=r.i32(1, 1000),
                       K2=r.i32(1, 1000), K3=r.i32(1, 1000)),
    ),

    # ----------------------------------------------------------------- callbacks
    "cb_qsort": _generator(
        "callbacks",
        r"""
#include <stdio.h>
#include <stdlib.h>
static int cmp_int(const void *pa, const void *pb) {
    int a = *(const int *)pa, b = *(const int *)pb;
    return (a > b) - (a < b);
}
int main(void) {
    int a[6] = {@A@, @B@, @C@, @D@, @E@, @F@};
    qsort(a, 6, sizeof(a[0]), cmp_int);
    printf("sorted=%d,%d,%d,%d,%d,%d\n", a[0], a[1], a[2], a[3], a[4], a[5]);
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 999), B=r.i32(1000, 1999),
                       C=r.i32(2000, 2999), D=r.i32(3000, 3999),
                       E=r.i32(4000, 4999), F=r.i32(5000, 5999)),
    ),
    "cb_table": _generator(
        "callbacks",
        r"""
#include <stdio.h>
static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
typedef int (*op_fn)(int, int);
int main(void) {
    op_fn table[3] = {add, sub, mul};
    int a = @A@, b = @B@;
    printf("ops=%d,%d,%d\n", table[0](a, b), table[1](a, b), table[2](a, b));
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 1000), B=r.i32(1, 1000)),
    ),
    "cb_nested": _generator(
        "callbacks",
        r"""
#include <stdio.h>
static int scale(int x) { return x * @K@; }
static int apply(int (*fn)(int), int x) { return fn(x); }
static int (*active)(int) = scale;
int main(void) {
    printf("nested=%d direct=%d\n", apply(active, @X@), active(@Y@));
    return 0;
}
""",
        lambda r: dict(K=r.i32(2, 9), X=r.i32(1, 1000), Y=r.i32(1, 1000)),
    ),

    # --------------------------------------------------------------------- stdio
    "stdio_formats": _generator(
        "stdio",
        r"""
#include <stdio.h>
int main(void) {
    int i = @I@;
    unsigned u = @U@u;
    long l = @L@L;
    printf("dec=%d hex=%x oct=%o\n", i, (unsigned)i, (unsigned)i);
    printf("uns=%u long=%ld char=%c str=%s\n", u, l, 'Q', "quoted");
    printf("pct=%% width=[%5d] zero=[%05d]\n", i, i);
    return 0;
}
""",
        lambda r: dict(I=r.i32(1, 99999), U=r.i32(1, 99999), L=r.i32(1, 99999)),
    ),
    "stdio_snprintf": _generator(
        "stdio",
        r"""
#include <stdio.h>
int main(void) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%s-%d-%d", "abcdefghij", @A@, @B@);
    printf("n=%d buf=%s\n", n, buf);
    printf("len=%zu\n", sizeof(buf));
    return 0;
}
""",
        lambda r: dict(A=r.i32(1, 9999), B=r.i32(1, 9999)),
    ),
    "stdio_lines": _generator(
        "stdio",
        r"""
#include <stdio.h>
int main(void) {
    puts("first");
    putchar('A'); putchar('\n');
    fprintf(stdout, "third=%d\n", @N@);
    printf("\nlast\n");
    return 0;
}
""",
        lambda r: dict(N=r.i32(1, 99999)),
    ),
    "stdio_stderr": _generator(
        "stdio",
        r"""
#include <stdio.h>
int main(void) {
    fprintf(stdout, "out=%d\n", @O@);
    fprintf(stderr, "err=%d\n", @E@);
    printf("out2=%d\n", @P@);
    return 0;
}
""",
        lambda r: dict(O=r.i32(1, 99999), E=r.i32(1, 99999),
                       P=r.i32(1, 99999)),
    ),
}


def build_corpus() -> list[Case]:
    cases = []
    for index, (name, generator) in enumerate(GENERATORS.items()):
        category, template, value_fn = generator
        seed = splitmix64(MASTER_SEED + index)
        values = value_fn(Rng(seed))
        source = render(template, **values)
        cases.append(Case(name=name, category=category, seed=seed,
                          source=source))
    return cases

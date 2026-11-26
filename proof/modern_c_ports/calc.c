/* Independent modern-C port of the calc sample workload: a small floating-point
 * expression interpreter with a scripted session. C11; the variable table is a
 * transient scoped-arena allocation. No inspiration-language source was copied. */
#include "common.h"
#include <math.h>
#include <stdarg.h>

static unsigned char arena_bytes[1 << 14];
static size_t arena_top;
static void *scoped_take(size_t n) {
    void *p = arena_bytes + arena_top;
    arena_top = (arena_top + n + 15u) & ~(size_t)15u;
    return p;
}

struct Var { char name[32]; double val; };
static struct Var *vars;
static int nvars;

typedef struct { const char *p; char err[80]; } Ctx;

static void fail(Ctx *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err, sizeof(c->err), fmt, ap);
    va_end(ap);
}
static void ws(Ctx *c) { while (*c->p == ' ' || *c->p == '\t') ++c->p; }
static double expr(Ctx *c);
static double unary(Ctx *c);

static double primary(Ctx *c) {
    char ch;
    ws(c);
    ch = *c->p;
    if (ch == '(') {
        double v;
        ++c->p;
        v = expr(c);
        if (c->err[0]) return 0.0;
        ws(c);
        if (*c->p != ')') { fail(c, "expected ')'"); return 0.0; }
        ++c->p;
        return v;
    }
    if (ch == '*') { fail(c, "unexpected '*'"); return 0.0; }
    if (ch == '-') { ++c->p; return -unary(c); }
    if (isdigit((unsigned char)ch) || ch == '.') {
        char *end;
        double v = strtod(c->p, &end);
        if (end == c->p) { fail(c, "bad number"); return 0.0; }
        c->p = end;
        return v;
    }
    if (isalpha((unsigned char)ch)) {
        char name[32];
        size_t len = 0;
        while (isalnum((unsigned char)*c->p) || *c->p == '_') {
            if (len + 1 < sizeof(name)) name[len++] = *c->p;
            ++c->p;
        }
        name[len] = 0;
        ws(c);
        if (*c->p == '(') {
            double v;
            ++c->p;
            v = expr(c);
            if (c->err[0]) return 0.0;
            ws(c);
            if (*c->p != ')') { fail(c, "expected ')'"); return 0.0; }
            ++c->p;
            if (strcmp(name, "sqrt") == 0) return sqrt(v);
            fail(c, "unknown function '%s'", name);
            return 0.0;
        }
        if (strcmp(name, "pi") == 0) return 3.14159265358979323846;
        for (int i = 0; i < nvars; ++i)
            if (strcmp(vars[i].name, name) == 0) return vars[i].val;
        fail(c, "unknown variable '%s'", name);
        return 0.0;
    }
    if (ch == 0) { fail(c, "unexpected end of input"); return 0.0; }
    fail(c, "unexpected character '%c'", ch);
    ++c->p;
    return 0.0;
}

static double unary(Ctx *c) {
    ws(c);
    if (*c->p == '-') { ++c->p; return -unary(c); }
    if (*c->p == '+') { ++c->p; return unary(c); }
    return primary(c);
}

static double power(Ctx *c) {
    double a = unary(c);
    if (c->err[0]) return 0.0;
    ws(c);
    if (*c->p == '^') {
        double b;
        ++c->p;
        b = power(c);
        if (c->err[0]) return 0.0;
        return pow(a, b);
    }
    return a;
}

static double term(Ctx *c) {
    double a = power(c);
    if (c->err[0]) return 0.0;
    for (;;) {
        char op;
        double b;
        ws(c);
        op = *c->p;
        if (op != '*' && op != '/') break;
        ++c->p;
        b = power(c);
        if (c->err[0]) return 0.0;
        if (op == '*') a *= b;
        else {
            if (b == 0.0) { fail(c, "division by zero"); return 0.0; }
            a /= b;
        }
    }
    return a;
}

static double expr(Ctx *c) {
    double a = term(c);
    if (c->err[0]) return 0.0;
    for (;;) {
        char op;
        double b;
        ws(c);
        op = *c->p;
        if (op != '+' && op != '-') break;
        ++c->p;
        b = term(c);
        if (c->err[0]) return 0.0;
        if (op == '+') a += b;
        else a -= b;
    }
    return a;
}

static void fmt(double v, char *out) {
    size_t n;
    if (fabs(v - round(v)) < 1e-9) { snprintf(out, 64, "%.0f", v); return; }
    snprintf(out, 64, "%.6f", v);
    n = strlen(out);
    while (n && out[n - 1] == '0') out[--n] = 0;
    if (n && out[n - 1] == '.') out[--n] = 0;
}

static int eval(const char *line, char *out, char *err) {
    Ctx c;
    double v;
    c.p = line;
    c.err[0] = 0;
    ws(&c);
    if (isalpha((unsigned char)*c.p)) {
        char name[32];
        size_t len = 0;
        const char *save = c.p;
        while (isalnum((unsigned char)*c.p) || *c.p == '_') {
            if (len + 1 < sizeof(name)) name[len++] = *c.p;
            ++c.p;
        }
        name[len] = 0;
        ws(&c);
        if (*c.p == '=') {
            ++c.p;
            v = expr(&c);
            if (c.err[0]) { strcpy(err, c.err); return -1; }
            for (int i = 0; i < nvars; ++i)
                if (strcmp(vars[i].name, name) == 0) { vars[i].val = v; break; }
            if (nvars < 16) {
                strcpy(vars[nvars].name, name);
                vars[nvars].val = v;
                ++nvars;
            }
            fmt(v, out);
            return 0;
        }
        c.p = save;
    }
    v = expr(&c);
    if (c.err[0]) { strcpy(err, c.err); return -1; }
    ws(&c);
    if (*c.p) {
        snprintf(err, sizeof(c.err), "unexpected character '%c'", *c.p);
        return -1;
    }
    fmt(v, out);
    return 0;
}

int main(void) {
    static const char *lines[] = {
        "1+2*3", "(1+2)*3", "2^10", "sqrt(2)", "0-1-2-3", "7", "7+8",
        "11/2", "2*pi", "r = 3", "area = pi*r^2", "area", "1/0", "2**3",
        "(1+2", "$", "z", "2.5", "quit"
    };
    char out[64], err[80];

    (void)fails; (void)report;
    vars = scoped_take(sizeof(struct Var) * 16);
    nvars = 0;

    printf("calc: expressions, `name = expr`, `quit`\n");
    for (unsigned i = 0; i < sizeof(lines) / sizeof(*lines); ++i) {
        printf("> ");
        if (strcmp(lines[i], "quit") == 0) { printf("bye\n"); break; }
        if (eval(lines[i], out, err) < 0) printf("error: %s\n", err);
        else printf("%s\n", out);
    }
    return 0;
}

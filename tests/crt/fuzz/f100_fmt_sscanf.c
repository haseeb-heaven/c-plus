/* F-049: cplus_sscanf_s over deterministic valid/invalid numeric and bounded
 * string tokens.  Formats are literals with explicit %s widths (Annex K), so
 * no unbounded write can occur; the return value is checked against [-1, 2]. */
#include "f100_fmt_common.h"

static const char *tokens[] = {
    "123", "-2147483648", "2147483647", "0", "007", "+19", "abc", "",
    "  42xyz", "3.14159", "-1e300", "0x2a", "18446744073709551615",
    "word;next", "short", "999999999999999999999999"
};
#define NTOK ((unsigned)(sizeof tokens / sizeof tokens[0]))

typedef struct {
    unsigned char guard_pre[16];
    char sbuf[64];
    unsigned char guard_post[16];
} guarded_str;

static void guards_init(guarded_str *g) { memset(g, F100_CANARY, sizeof *g); }
static int guards_ok(const guarded_str *g) {
    size_t i;
    for (i = 0; i < sizeof g->guard_pre; ++i)
        if (g->guard_pre[i] != F100_CANARY) return 0;
    for (i = 0; i < sizeof g->guard_post; ++i)
        if (g->guard_post[i] != F100_CANARY) return 0;
    return 1;
}

int main(void) {
    guarded_str g;
    int a, b;
    unsigned u;
    long long ll;
    double d;
    unsigned i, t;
    int rc;

    f100_init();
    for (i = 0; i < 1250; ++i) {
        t = f100_below(NTOK);
        guards_init(&g);
        a = b = 0; u = 0; ll = 0; d = 0.0;
        switch (f100_below(6)) {
        case 0: rc = cplus_sscanf_s(tokens[t], "%d", &a); break;
        case 1: rc = cplus_sscanf_s(tokens[t], "%u", &u); break;
        case 2: rc = cplus_sscanf_s(tokens[t], "%lld", &ll); break;
        case 3: rc = cplus_sscanf_s(tokens[t], "%lf", &d); break;
        case 4: rc = cplus_sscanf_s(tokens[t], "%63s", g.sbuf); break;
        default: rc = cplus_sscanf_s(tokens[t], "%d %d", &a, &b); break;
        }
        F100_OP();
        if (rc < -1 || rc > 2) F100_FAIL("sscanf_s return outside [-1,2]");
        if (!guards_ok(&g)) F100_FAIL("sscanf_s wrote past bounded destination");
        (void)a; (void)b; (void)u; (void)ll; (void)d;
    }
    return f100_report("F-049");
}

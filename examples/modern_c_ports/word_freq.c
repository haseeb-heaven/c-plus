/* Independent modern-C port of the "word_freq" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

struct freq { const char *w; int n; };
int main(void) {
    const char *text = "scoped c scoped port";
    struct freq f[3] = {{"scoped", 0}, {"c", 0}, {"port", 0}};
    char word[32]; int i;
    const char *p = text;
    while (*p) {
        const char *tok; size_t len;
        p += strspn(p, " \t\n");
        if (!*p) break;
        tok = p; len = strcspn(p, " \t\n");
        if (len < sizeof(word)) {
            memcpy(word, tok, len); word[len] = 0;
            for (i = 0; i < 3; ++i)
                if (strcmp(word, f[i].w) == 0) f[i].n++;
        }
        p += len;
    }
    for (i = 0; i < 3; ++i) printf("word_freq %s %d\n", f[i].w, f[i].n);
    CHECK("word_freq_top", f[0].n == 2); report("WORD_FREQ"); return fails;
}

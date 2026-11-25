/* Independent modern-C port of the "huffman" Goose sample workload.
 * C11, no Goose source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

struct sym { char ch; int freq; const char *code; };
int main(void) {
    struct sym s[3] = {{'a', 8, "0"}, {'b', 3, "10"}, {'c', 1, "11"}};
    int total = 0, bits = 0, i;
    for (i = 0; i < 3; ++i) {
        total += s[i].freq;
        bits += s[i].freq * (int)strlen(s[i].code);
        printf("huffman %c %d %s\n", s[i].ch, s[i].freq, s[i].code);
    }
    printf("huffman total %d bits %d\n", total, bits);
    CHECK("huffman_bits", bits == 16); report("HUFFMAN"); return fails;
}

#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

int main(void) {
    char line[128];
    char tok[64];
    int i;
    while (fgets(line, (int)sizeof(line), stdin) != NULL) {
        string ln = string(line);
        for (i = 0; i < 4; i++) {
            if (ln.field(' ', (size_t)i, tok, sizeof(tok)) != 0) break;
            print("TOK", i, tok);
        }
        ln.drop();
    }
    return 0;
}

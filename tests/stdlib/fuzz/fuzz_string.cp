/* libFuzzer target: string/vector growth and parsing on arbitrary bytes. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    error err = error();
    string s = string("");
    vector v = vector();
    char piece[2];
    char out[8];
    size_t i;

    piece[1] = '\0';
    for (i = 0; i < size; i++) {
        piece[0] = (char)data[i];
        switch (data[i] % 6) {
        case 0:
            s.append(piece);
            break;
        case 1:
            s.reserve((size_t)data[i]);
            break;
        case 2:
            s.substr(0, (size_t)data[i], out, sizeof(out));
            break;
        case 3:
            s.field((char)data[i], (size_t)(data[i] % 4), out, sizeof(out));
            break;
        case 4:
            v.push_back((int)data[i]);
            break;
        default: {
            int popped = 0;
            v.pop_back(&popped);
            v.at_checked((size_t)data[i], &popped);
            break;
        }
        }
        if (s.size() > 4096)
            s.clear();
        if (v.size() > 1024)
            v.clear();
    }
    {
        string_view view = s.to_view();
        (void)view.size();
        (void)view.equals("x");
    }
    return 0;
}

/* libFuzzer target: the JSON wrapper on arbitrary bytes.
 * Build/run with tests/stdlib/fuzz/run_fuzz.sh. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    error err = error();
    json doc = json((const char *)data, &err);
    if (doc.valid()) {
        json_view root = doc.root();
        size_t count = root.size();
        size_t i;
        for (i = 0; i < count && i < 16; i++) {
            json_view item = root.at(i);
            int value = 0;
            item.get_int(&value, &err);
        }
        doc.drop();
    }
    return 0;
}

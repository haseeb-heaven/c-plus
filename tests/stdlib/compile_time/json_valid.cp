/* EXPECT: ACCEPT
 *
 * Positive control for stdlib/json.hp: parse an owned document, read it
 * through views that stay inside the owner's lifetime, serialise a view and
 * release the returned buffer with json_free().  The builder is used the
 * same way: write, then json_free().  No ownership/lifetime diagnostic is
 * expected from c+ check --strict or c+ build --strict --sanitize=none.
 *
 * Build flags required by this header (see json.json):
 *   -Ithird_party/yyjson -Lbuild/ext -lyyjson
 */
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int read_count(json_view root, error *err) {
    json_view n = root.member("n");
    int count = -1;
    if (n.get_int(&count, err) != STDLIB_OK)
        return -1;
    return count;
}

int main(void) {
    error err = error();
    json doc = json("{\"n\":7,\"name\":\"cplus\",\"tags\":[\"ok\"]}", &err);
    if (!doc.valid())
        return 1;

    json_view root = doc.root();
    if (read_count(root, &err) != 7)
        return 2;

    json_view name = root.member("name");
    const char *name_text = NULL;
    if (name.get_string(&name_text, &err) != STDLIB_OK || name_text == NULL)
        return 3;
    if (strcmp(name_text, "cplus") != 0)
        return 4;

    size_t length = 0;
    char *dump = root.write(&length);
    if (dump == NULL || length == 0)
        return 5;
    json_free(dump);

    json_builder builder = json_builder();
    if (builder.add_int("n", 7, &err) != STDLIB_OK)
        return 6;
    char *built = builder.write(&length);
    if (built == NULL || length == 0)
        return 7;
    json_free(built);

    return 0; /* doc and builder are released by C+ RAII at scope exit */
}

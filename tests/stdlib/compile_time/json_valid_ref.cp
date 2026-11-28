/* EXPECT: ACCEPT
 *
 * Positive control specifically for json_view borrowing.  Every view is
 * created from a document that is still alive, intermediate views are read
 * before the document leaves scope, and the borrowed string is consumed
 * while the owner is live.  This is the valid counterpart of
 * json_dangling_ref.cp.  No [CPLUS- diagnostic is expected.
 *
 * Build flags required by this header (see json.json):
 *   -Ithird_party/yyjson -Lbuild/ext -lyyjson
 */
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int first_tag_is(json_view tags, const char *expected, error *err) {
    json_view first = tags.at(0);       /* view of a view: still borrowed */
    const char *value = NULL;
    if (first.get_string(&value, err) != STDLIB_OK || value == NULL)
        return 0;
    return strcmp(value, expected) == 0;
}

int main(void) {
    error err = error();
    json doc = json("{\"project\":\"igi\",\"tags\":[\"alpha\",\"beta\"]}", &err);
    if (!doc.valid())
        return 1;

    json_view root = doc.root();
    json_view project = root.member("project");
    json_view tags = root.member("tags");

    const char *name = NULL;
    if (project.get_string(&name, &err) != STDLIB_OK || name == NULL)
        return 2;
    if (strcmp(name, "igi") != 0)
        return 3;

    if (!tags.is_array() || tags.size() != 2)
        return 4;
    if (!first_tag_is(tags, "alpha", &err))
        return 5;

    /* All views stop being used before doc is destroyed at scope exit. */
    return 0;
}

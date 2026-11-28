/* Demo: RAII JSON - parse, borrow values, build and serialise. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    error err = error();
    const char *text =
        "{\"project\":\"cplus\",\"stars\":5,\"tags\":[\"safe\",\"small\"]}";
    json doc = json(text, &err);
    if (!doc.valid()) {
        print("parse failed", err.what());
        return 1;
    }

    json_view root = doc.root();
    json_view project_ref = root.member("project");
    json_view stars_ref = root.member("stars");
    const char *project = NULL;
    int stars = 0;
    project_ref.get_string(&project, &err);
    stars_ref.get_int(&stars, &err);
    print("project", project, "stars", stars, "object", root.is_object());

    json_view tags = root.member("tags");
    size_t total = tags.size();
    size_t i;
    for (i = 0; i < total; i++) {
        json_view tag = tags.at(i);
        const char *value = NULL;
        tag.get_string(&value, &err);
        print(" tag", i, value);
    }

    json_builder builder = json_builder();
    builder.add_string("language", "C+", &err);
    builder.add_int("year", 2026, &err);
    size_t length = 0;
    char *out = builder.write(&length);
    print("built", out);
    json_free(out);

    json bad = json("{oops", &err);
    print("bad json", bad.valid(), err.failed_now());
    return 0;
}

/* JSON wrapper test: parse (own), read (borrow), build, serialise, fail. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    error err = error();
    const char *text =
        "{\"name\":\"cplus\",\"stars\":5,\"ratio\":1.5,\"ok\":true,"
        "\"tags\":[\"a\",\"b\",\"c\"]}";

    json doc = json(text, &err);
    if (!doc.valid()) {
        print("parse-failed", err.what());
        return 1;
    }

    json_view root = doc.root();
    json_view name_ref = root.member("name");
    json_view stars_ref = root.member("stars");
    json_view ratio_ref = root.member("ratio");
    json_view ok_ref = root.member("ok");
    json_view tags_ref = root.member("tags");

    const char *name = NULL;
    int stars = -1;
    double ratio = 0;
    int ok = 0;
    int got_name = name_ref.get_string(&name, &err);
    int got_stars = stars_ref.get_int(&stars, &err);
    int got_ratio = ratio_ref.get_double(&ratio, &err);
    int got_ok = ok_ref.get_bool(&ok, &err);
    print("read", got_name, name, got_stars, stars, got_ratio, ratio, got_ok, ok);

    size_t tags = tags_ref.size();
    json_view first = tags_ref.at(0);
    const char *first_tag = NULL;
    first.get_string(&first_tag, &err);
    print("array", tags, first_tag, tags_ref.valid(), tags_ref.is_array(),
          root.is_object(), stars_ref.is_number(), name_ref.is_string(),
          ok_ref.is_bool());

    json_view missing = root.member("nope");
    int missing_hit = missing.get_int(&stars, &err);
    print("missing", missing.valid(), missing_hit);

    json file_doc = json_parse_file("tests/stdlib/data/sample.json", &err);
    json_view file_root = file_doc.root();
    json_view project = file_root.member("project");
    const char *project_name = NULL;
    project.get_string(&project_name, &err);
    print("file", file_doc.valid(), project_name);

    size_t length = 0;
    char *dump = root.write(&length);
    print("dump", dump, length);
    json_free(dump);

    json_builder builder = json_builder();
    builder.add_string("lang", "cplus", &err);
    builder.add_int("year", 2026, &err);
    builder.add_double("version", 0.1, &err);
    builder.add_bool("safe", 1, &err);
    char *built = builder.write(&length);
    print("built", built, builder.valid());
    json_free(built);

    json bad = json("{not json", &err);
    print("bad", bad.valid(), err.failed_now(), err.what());
    return 0;
}

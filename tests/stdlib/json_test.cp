/* JSON wrapper test: parse (own), read (borrow), build, serialise, fail. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    StdError err;
    StdError_init(&err);

    const char *text =
        "{\"name\":\"cplus\",\"stars\":5,\"ratio\":1.5,\"ok\":true,"
        "\"tags\":[\"a\",\"b\",\"c\"]}";

    JsonDoc doc = JsonDoc_parse(text, &err);
    if (!JsonDoc_valid(&doc)) {
        print("parse-failed", StdError_message(&err));
        return 1;
    }

    JsonRef root = JsonDoc_root(&doc);
    JsonRef name_ref = JsonRef_member(&root, "name");
    JsonRef stars_ref = JsonRef_member(&root, "stars");
    JsonRef ratio_ref = JsonRef_member(&root, "ratio");
    JsonRef ok_ref = JsonRef_member(&root, "ok");
    JsonRef tags_ref = JsonRef_member(&root, "tags");

    const char *name = NULL;
    int stars = -1;
    double ratio = 0;
    int ok = 0;
    int got_name = JsonRef_str(&name_ref, &name);
    int got_stars = JsonRef_int(&stars_ref, &stars);
    int got_ratio = JsonRef_double(&ratio_ref, &ratio);
    int got_ok = JsonRef_bool(&ok_ref, &ok);
    print("read", got_name, name, got_stars, stars, got_ratio, ratio, got_ok,
          ok);

    size_t tag_count = JsonRef_size(&tags_ref);
    JsonRef first = JsonRef_arr_at(&tags_ref, 0);
    const char *first_tag = NULL;
    JsonRef_str(&first, &first_tag);
    print("array", tag_count, first_tag, JsonRef_valid(&tags_ref));

    JsonRef missing = JsonRef_member(&root, "nope");
    int missing_hit = JsonRef_int(&missing, &stars);
    print("missing", JsonRef_valid(&missing), missing_hit);

    JsonDoc file_doc = JsonDoc_read_file("tests/stdlib/data/sample.json", &err);
    JsonRef file_root = JsonDoc_root(&file_doc);
    JsonRef project = JsonRef_member(&file_root, "project");
    const char *project_name = NULL;
    JsonRef_str(&project, &project_name);
    print("file", JsonDoc_valid(&file_doc), project_name);

    size_t len = 0;
    char *dump = JsonRef_write(&root, &len);
    print("dump", dump, len);
    Json_free(dump);

    JsonBuilder builder = JsonBuilder_new();
    JsonBuilder_add_str(&builder, "lang", "cplus");
    JsonBuilder_add_int(&builder, "year", 2026);
    JsonBuilder_add_bool(&builder, "safe", 1);
    char *built = JsonBuilder_write(&builder, &len);
    print("built", built);
    Json_free(built);

    JsonDoc bad = JsonDoc_parse("{not json", &err);
    print("bad", JsonDoc_valid(&bad), StdError_failed(&err),
          StdError_message(&err));
    return 0;
}

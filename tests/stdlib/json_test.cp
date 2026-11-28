/* JSON wrapper test: parse (own), read (borrow), build, serialise, fail. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

/* Issue #57 regression: json_builder must copy keys into document-owned
 * storage, not retain the caller's raw pointer. Every key below lives only
 * in jk_add_transients' stack frame; once it returns the keys are dead, and
 * jk_reuse_stack overwrites that same stack region. A builder that kept the
 * raw pointers would serialise filler bytes (or fail outright); a builder
 * that duplicates keys emits the originals regardless of the dead frame. */
static void jk_add_transients(json_builder *builder, error *err) {
    char key[32];
    int i;
    for (i = 0; i < 32; i++)
        key[i] = 0;
    key[0] = 'k'; key[1] = '1';
    builder->add_int(key, 7, err);
    key[1] = '2';
    builder->add_double(key, 2.5, err);
    key[1] = '3';
    builder->add_bool(key, 1, err);
    key[1] = '4';
    builder->add_string(key, "v", err);
} /* the stack key dies here */

static void jk_reuse_stack(void) {
    char scratch[32];
    int i;
    for (i = 0; i < 32; i++)
        scratch[i] = 'X';
}

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

    /* Serialise only after every key's stack frame is gone and clobbered. */
    json_builder key_builder = json_builder();
    char *key_dump;
    size_t key_length = 0;
    jk_add_transients(&key_builder, &err);
    jk_reuse_stack();
    key_dump = key_builder.write(&key_length);
    print("builder_key_lifetime", key_dump, key_length);
    json_free(key_dump);
    key_builder.drop();

    json bad = json("{not json", &err);
    print("bad", bad.valid(), err.failed_now(), err.what());
    return 0;
}

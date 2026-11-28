/* Hostile-input corpus: stdlib/json.hp (Task 6 Phase 2).
 *
 * Self-checking: each case prints "<tag> <0|1>"; final line is
 * "HOSTILE_JSON <checks> <failures>". Exit 0 only when failures == 0.
 * Every probe uses a recoverable API result or a metadata query; no check
 * dereferences an unproven pointer or performs out-of-bounds access.
 *
 * Build: ./build/c+ build <this file> --strict --sanitize=none \
 *            -Ithird_party/yyjson -Lbuild/ext -lyyjson -o <bin>
 * Run (from repo root): <bin> [fixture_dir]
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"

static int hj_checks = 0;
static int hj_failures = 0;

static void hj_check(const char *tag, int cond) {
    hj_checks++;
    if (!cond)
        hj_failures++;
    print(tag, cond ? 1 : 0);
}

static char hj_base_path[512];
static char hj_fixture_path[640];

static char *hj_argv_ptr(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static unsigned char hj_byte(const char *p, size_t idx) {
    unsigned char b;
    __builtin_memcpy(&b, p + idx, 1);
    return b;
}

/* Returns byte count, or -1 when the fixture cannot be opened/read. */
static long hj_load(const char *name, char *dst, size_t cap) {
    error load_err = error();
    int path_len;
    long before_pos;
    long after_pos;
    size_t total_bytes = 0;
    int read_rc;
    path_len = snprintf(hj_fixture_path, sizeof(hj_fixture_path), "%s/%s",
                        hj_base_path, name);
    if (path_len <= 0 || (size_t)path_len >= sizeof(hj_fixture_path))
        return -1;
    {
        file load_handle = file(hj_fixture_path, "rb");
        if (!load_handle.is_open())
            return -1;
        for (;;) {
            if (total_bytes >= cap)
                break;
            before_pos = ftell(load_handle.native());
            if (before_pos < 0) {
                load_handle.drop();
                return -1;
            }
            read_rc = load_handle.read(dst + total_bytes, cap - total_bytes,
                                       &load_err);
            after_pos = ftell(load_handle.native());
            if (after_pos < before_pos) {
                load_handle.drop();
                return -1;
            }
            total_bytes += (size_t)(after_pos - before_pos);
            if (read_rc == STDLIB_EOF || after_pos == before_pos)
                break;
            if (read_rc != STDLIB_OK) {
                load_handle.drop();
                return -1;
            }
        }
        load_handle.drop();
    }
    if (total_bytes < cap) {
        char term = '\0';
        __builtin_memcpy(dst + total_bytes, &term, 1);
    }
    return (long)total_bytes;
}

int main(int argc, char **argv) {
    char large_doc[1048578];

    if (argc > 1) {
        int base_len = snprintf(hj_base_path, sizeof(hj_base_path), "%s",
                                hj_argv_ptr(argv, 1));
        if (base_len <= 0 || (size_t)base_len >= sizeof(hj_base_path))
            return 2;
    } else {
        int base_len = snprintf(hj_base_path, sizeof(hj_base_path), "%s",
                                "tests/security/hostile_inputs/fixtures");
        if (base_len <= 0 || (size_t)base_len >= sizeof(hj_base_path))
            return 2;
    }

    /* ---- NULL / empty / truncated / unterminated ----------------------- */
    {
        error null_err = error();
        json null_doc = json(NULL, &null_err);
        hj_check("json_null_text_invalid", null_doc.valid() == 0);
        /* Documented: a NULL source does not record an error. */
        hj_check("json_null_text_no_error", null_err.ok() == 1);
        null_doc.drop();
    }
    {
        error bad_err = error();
        json bad_doc = json("", &bad_err);
        hj_check("json_empty_invalid", bad_doc.valid() == 0 &&
                 bad_err.failed_now() == 1 &&
                 bad_err.category() == STDLIB_CATEGORY_INVALID);
        bad_doc.drop();
    }
    {
        error trunc_err = error();
        json trunc_doc = json("{", &trunc_err);
        hj_check("json_truncated_object", trunc_doc.valid() == 0 &&
                 trunc_err.failed_now() == 1);
        trunc_doc.drop();
    }
    {
        error arr_err = error();
        json arr_doc = json("[1,2", &arr_err);
        hj_check("json_truncated_array", arr_doc.valid() == 0 &&
                 arr_err.failed_now() == 1);
        arr_doc.drop();
    }
    {
        char bad_escape[] = {'{','"','a','"',':','"','\\','q','"','}',0};
        error esc_err = error();
        json esc_doc = json(bad_escape, &esc_err);
        hj_check("json_bad_escape", esc_doc.valid() == 0 &&
                 esc_err.failed_now() == 1);
        esc_doc.drop();
    }

    /* ---- fixture-backed malformed documents ---------------------------- */
    {
        char bad_doc_buf[128];
        long bad_count = hj_load("json_unterminated.json", bad_doc_buf,
                                 sizeof(bad_doc_buf));
        error unterminated_err = error();
        json unterminated_doc = json(bad_doc_buf, &unterminated_err);
        hj_check("json_fixture_loaded", bad_count == 19);
        hj_check("json_unterminated_string",
                 unterminated_doc.valid() == 0 &&
                 unterminated_err.failed_now() == 1);
        unterminated_doc.drop();
    }

    /* ---- embedded NUL: prefix parses, remainder ignored ---------------- */
    {
        char nul_buf[64];
        long nul_count = hj_load("json_embedded_nul.json", nul_buf,
                                 sizeof(nul_buf));
        error nul_err = error();
        json nul_doc = json(nul_buf, &nul_err);
        int nul_ok = nul_count == 15 && nul_doc.valid() == 1;
        if (nul_ok) {
            json_view nul_root = nul_doc.root();
            json_view nul_b = nul_root.member("b");
            nul_ok = nul_root.size() == 1 && nul_b.valid() == 0;
        }
        hj_check("json_embedded_nul_prefix", nul_ok);
        nul_doc.drop();
    }
    {
        char nul_bad_buf[32];
        long nul_bad_count = hj_load("json_embedded_nul_bad.json",
                                     nul_bad_buf, sizeof(nul_bad_buf));
        error nul_bad_err = error();
        json nul_bad_doc = json(nul_bad_buf, &nul_bad_err);
        hj_check("json_embedded_nul_incomplete",
                 nul_bad_count == 8 && nul_bad_doc.valid() == 0);
        nul_bad_doc.drop();
    }

    /* ---- invalid UTF-8 encoding ---------------------------------------- */
    {
        char utf8_buf[64];
        long utf8_count = hj_load("json_invalid_utf8.json", utf8_buf,
                                  sizeof(utf8_buf));
        error utf8_err = error();
        json utf8_doc = json(utf8_buf, &utf8_err);
        int first_byte;
        int doc_valid = utf8_doc.valid();
        first_byte = hj_byte(utf8_buf, 0);
        /* The fixture is a syntactically complete object whose string holds an
         * overlong 2-byte sequence. Rejection is the defensive outcome; if the
         * upstream parser accepts it, the check records that behaviour and the
         * finding is reported in NOTES.md. */
        hj_check("json_invalid_utf8_rejected", utf8_count == 10 &&
                 doc_valid == 0);
        hj_check("json_invalid_utf8_err_meta", doc_valid == 0
                     ? utf8_err.failed_now() == 1
                     : utf8_err.ok() == 1);
        hj_check("json_invalid_utf8_fixture_intact", first_byte == '{');
        utf8_doc.drop();
    }

    /* ---- bounded deep nesting ------------------------------------------ */
    {
        char deep_buf[1024];
        long deep_count = hj_load("json_deep_256.json", deep_buf,
                                  sizeof(deep_buf));
        error deep_err = error();
        json deep_doc = json(deep_buf, &deep_err);
        hj_check("json_deep_256", deep_count == 513 && deep_doc.valid() == 1);
        deep_doc.drop();
    }
    {
        char deeper_buf[16384];
        long deeper_count = hj_load("json_deep_4096.json", deeper_buf,
                                    sizeof(deeper_buf));
        error deeper_err = error();
        json deeper_doc = json(deeper_buf, &deeper_err);
        hj_check("json_deep_4096", deeper_count == 8193 &&
                 deeper_doc.valid() == 1);
        deeper_doc.drop();
    }

    /* ---- excessively large, bounded input (<= 1 MiB) ------------------- */
    {
        long large_count = hj_load("json_large_1mib.json", large_doc,
                                   sizeof(large_doc));
        error large_err = error();
        json large_doc_parsed = json(large_doc, &large_err);
        int large_ok = large_count == 1048575 && large_doc_parsed.valid() == 1;
        if (large_ok) {
            json_view large_root = large_doc_parsed.root();
            large_ok = large_root.size() == 524287 && large_root.is_array() == 1;
        }
        hj_check("json_large_1mib", large_ok);
        large_doc_parsed.drop();
    }

    /* ---- numeric overflow via typed getters ---------------------------- */
    {
        error oob_err = error();
        json oob_doc = json("{\"n\":2147483648}", &oob_err);
        int oob_value = 0;
        int oob_rc = -2;
        if (oob_doc.valid()) {
            json_view oob_root = oob_doc.root();
            json_view oob_v = oob_root.member("n");
            oob_rc = oob_v.get_int(&oob_value, &oob_err);
        }
        hj_check("json_get_int_overflow", oob_rc == STDLIB_FAIL &&
                 oob_err.category() == STDLIB_CATEGORY_RANGE);
        oob_doc.drop();
    }
    {
        error neg_err = error();
        json neg_doc = json("{\"n\":-9223372036854775808}", &neg_err);
        int neg_value = 0;
        int neg_rc = -2;
        if (neg_doc.valid()) {
            json_view neg_root = neg_doc.root();
            json_view neg_v = neg_root.member("n");
            neg_rc = neg_v.get_int(&neg_value, &neg_err);
        }
        hj_check("json_get_int_int64min", neg_rc == STDLIB_FAIL &&
                 neg_err.category() == STDLIB_CATEGORY_RANGE);
        neg_doc.drop();
    }

    /* ---- NULL arguments and invalid views ------------------------------ */
    {
        error view_err = error();
        json view_doc = json("[1]", &view_err);
        int view_value = 0;
        if (view_doc.valid()) {
            json_view view_root = view_doc.root();
            json_view at_oob = view_root.at(999);
            json_view null_key = view_root.member(NULL);
            hj_check("json_at_oob", at_oob.valid() == 0);
            hj_check("json_member_null_key", null_key.valid() == 0);
            hj_check("json_get_int_on_invalid",
                     at_oob.get_int(&view_value, &view_err) == STDLIB_FAIL);
        }
        view_doc.drop();
    }
    {
        json_view empty_view = json_view_of(NULL);
        size_t written_len = 0;
        char *written = empty_view.write(&written_len);
        hj_check("json_view_of_null", empty_view.valid() == 0);
        hj_check("json_write_invalid_view", written == NULL && written_len == 0);
        json_free(NULL);
        hj_check("json_free_null_noop", 1);
    }
    {
        error pf_err = error();
        json pf_doc = json_parse_file(NULL, &pf_err);
        hj_check("json_parse_file_null", pf_doc.valid() == 0 &&
                 pf_err.category() == STDLIB_CATEGORY_INVALID);
        pf_doc.drop();
    }
    {
        char missing_path[512];
        int path_len = snprintf(missing_path, sizeof(missing_path),
                                "%s/definitely_missing.json", hj_base_path);
        if (path_len > 0 && (size_t)path_len < sizeof(missing_path)) {
            error pf_err = error();
            json pf_doc = json_parse_file(missing_path, &pf_err);
            hj_check("json_parse_file_missing", pf_doc.valid() == 0 &&
                     pf_err.category() == STDLIB_CATEGORY_IO);
            pf_doc.drop();
        } else {
            hj_check("json_parse_file_missing", 0);
        }
    }

    /* ---- builder hostile arguments ------------------------------------- */
    {
        error build_err = error();
        json_builder builder = json_builder();
        hj_check("json_builder_add_int_null_key",
                 builder.add_int(NULL, 1, &build_err) == STDLIB_FAIL);
        hj_check("json_builder_add_string_null_value",
                 builder.add_string("k", NULL, &build_err) == STDLIB_FAIL);
        builder.drop();
        builder.drop();
        hj_check("json_builder_double_drop",
                 builder.valid() == 0);
    }

    print("HOSTILE_JSON", hj_checks, hj_failures);
    return hj_failures == 0 ? 0 : 1;
}

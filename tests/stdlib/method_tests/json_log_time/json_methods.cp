/* method_tests/json_log_time/json_methods.cp
 *
 * One deterministic case per public function/method declared in stdlib/json.hp.
 * Each case CALLS the method and verifies its result (return value and/or
 * observable state), then prints:  json <method> PASS|FAIL
 * The process exits nonzero if any case fails.  All inputs are fixed fixtures.
 * NOTE: variable names are globally unique across blocks because the C+
 * lowering pass uses a flat symbol table; temporaries are assigned to locals
 * before member calls because chained member calls are not lowered.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int failures = 0;

#define REPORT(method, cond)                                                   \
    do {                                                                       \
        int _ok = (cond) ? 1 : 0;                                              \
        print("json", method, _ok ? "PASS" : "FAIL");                          \
        if (!_ok)                                                              \
            failures++;                                                        \
    } while (0)

static const char *FIXTURE =
    "{\"name\":\"cplus\",\"stars\":5,\"ratio\":1.5,\"ok\":true,"
    "\"tags\":[\"a\",\"b\",\"c\"],\"nested\":{\"x\":7}}";

static const char *FILE_FIXTURE =
    "{ \"project\": \"cplus\", \"version\": 1, \"stable\": true }";

static const char *FILE_PATH = "/tmp/cplus_jlt_json_parse_fixture.json";

int main(void) {
    error err = error();

    /* ---- free functions ------------------------------------------------ */

    /* json_input_within_budget: accept in-budget, reject over-depth/empty. */
    {
        int ib_r1 = json_input_within_budget(FIXTURE, strlen(FIXTURE), 16, &err);
        int ib_ok1 = ib_r1 == 1 && err.ok();
        int ib_r2 = json_input_within_budget("[[[", 3, 2, &err);
        int ib_ok2 = ib_r2 == 0 && err.failed_now();
        int ib_r3 = json_input_within_budget("", 0, 16, &err);
        int ib_ok3 = ib_r3 == 0 && err.failed_now();
        REPORT("json_input_within_budget", ib_ok1 && ib_ok2 && ib_ok3);
    }

    /* json_parse_budget: explicit-length parse + budget failure path. */
    {
        json pb_doc = json_parse_budget(FIXTURE, strlen(FIXTURE), 16, &err);
        json_view pb_root = pb_doc.root();
        json_view pb_name = pb_root.member("name");
        const char *pb_text = NULL;
        int pb_got = pb_name.get_string(&pb_text, &err);
        int pb_ok = pb_doc.valid() && err.ok() && pb_root.is_object() &&
                    pb_got == STDLIB_OK && pb_text != NULL &&
                    strcmp(pb_text, "cplus") == 0;
        json pb_bad = json_parse_budget("[[[", 3, 2, &err);
        pb_ok = pb_ok && !pb_bad.valid() && err.failed_now();
        REPORT("json_parse_budget", pb_ok);
        pb_doc.drop();
        pb_bad.drop();
    }

    /* json_parse_file: parse a fixed fixture written to a temp path. */
    {
        FILE *pf_writer = fopen(FILE_PATH, "w");
        int pf_wrote = 0;
        if (pf_writer != NULL) {
            size_t pf_want = strlen(FILE_FIXTURE);
            pf_wrote = fwrite(FILE_FIXTURE, 1, pf_want, pf_writer) == pf_want;
            fclose(pf_writer);
        }
        json pf_doc = json_parse_file(FILE_PATH, &err);
        json_view pf_root = pf_doc.root();
        json_view pf_project = pf_root.member("project");
        json_view pf_version = pf_root.member("version");
        const char *pf_pname = NULL;
        int pf_pver = -1;
        int pf_gp = pf_project.get_string(&pf_pname, &err);
        int pf_gv = pf_version.get_int(&pf_pver, &err);
        int pf_ok = pf_wrote && pf_doc.valid() && err.ok() && pf_gp == STDLIB_OK &&
                    pf_gv == STDLIB_OK && pf_pname != NULL &&
                    strcmp(pf_pname, "cplus") == 0 && pf_pver == 1;
        json pf_missing = json_parse_file("/no/such/dir/cplus_jlt.json", &err);
        pf_ok = pf_ok && !pf_missing.valid() &&
                err.category() == STDLIB_CATEGORY_IO;
        REPORT("json_parse_file", pf_ok);
        pf_doc.drop();
        pf_missing.drop();
        remove(FILE_PATH);
    }

    /* json_view_of: wrap a raw yyjson_val obtained from an upstream doc. */
    {
        yyjson_doc *vo_raw =
            yyjson_read(FIXTURE, strlen(FIXTURE), YYJSON_READ_NOFLAG);
        int vo_ok = vo_raw != NULL;
        if (vo_raw != NULL) {
            json_view vo_view = json_view_of(yyjson_doc_get_root(vo_raw));
            json_view vo_stars = vo_view.member("stars");
            int vo_value = -1;
            int vo_got = vo_stars.get_int(&vo_value, &err);
            vo_ok = vo_view.valid() && vo_view.is_object() && vo_got == STDLIB_OK &&
                    vo_value == 5;
            yyjson_doc_free(vo_raw);
        }
        REPORT("json_view_of", vo_ok);
    }

    /* json_free: release a write() buffer safely (void: verify preconditions
     * and that the call returns; also verify json_free(NULL) is inert). */
    {
        json jf_doc = json(FIXTURE, &err);
        json_view jf_root = jf_doc.root();
        size_t jf_length = 0;
        char *jf_buffer = jf_root.write(&jf_length);
        int jf_ok = jf_doc.valid() && jf_buffer != NULL && jf_length > 0;
        json_free(jf_buffer);
        json_free(NULL);
        REPORT("json_free", jf_ok);
        jf_doc.drop();
    }

    /* ---- json_view methods --------------------------------------------- */

    /* json_view::json_view: NULL value -> invalid, real value -> valid. */
    {
        json_view vctor_empty = json_view((const yyjson_val *)0);
        yyjson_doc *vctor_raw =
            yyjson_read(FIXTURE, strlen(FIXTURE), YYJSON_READ_NOFLAG);
        int vctor_ok = !vctor_empty.valid() && vctor_raw != NULL;
        if (vctor_raw != NULL) {
            json_view vctor_real = json_view(yyjson_doc_get_root(vctor_raw));
            vctor_ok = vctor_ok && vctor_real.valid();
            yyjson_doc_free(vctor_raw);
        }
        REPORT("json_view::json_view", vctor_ok);
    }

    /* json_view::valid: present member valid, missing/out-of-range invalid. */
    {
        json vvalid_doc = json(FIXTURE, &err);
        json_view vvalid_root = vvalid_doc.root();
        json_view vvalid_present = vvalid_root.member("name");
        json_view vvalid_absent = vvalid_root.member("absent");
        json_view vvalid_far = vvalid_root.at(999);
        REPORT("json_view::valid", vvalid_present.valid() &&
                                       !vvalid_absent.valid() &&
                                       !vvalid_far.valid());
        vvalid_doc.drop();
    }

    /* json_view::is_object / is_array / is_number / is_string / is_bool. */
    {
        json vkind_doc = json(FIXTURE, &err);
        json_view vkind_root = vkind_doc.root();
        json_view vkind_tags = vkind_root.member("tags");
        json_view vkind_stars = vkind_root.member("stars");
        json_view vkind_name = vkind_root.member("name");
        json_view vkind_flag = vkind_root.member("ok");
        REPORT("json_view::is_object",
               vkind_root.is_object() && !vkind_stars.is_object());
        REPORT("json_view::is_array",
               vkind_tags.is_array() && !vkind_root.is_array());
        REPORT("json_view::is_number",
               vkind_stars.is_number() && !vkind_name.is_number());
        REPORT("json_view::is_string",
               vkind_name.is_string() && !vkind_stars.is_string());
        REPORT("json_view::is_bool",
               vkind_flag.is_bool() && !vkind_stars.is_bool());
        vkind_doc.drop();
    }

    /* json_view::get_int: value + error paths (non-int). */
    {
        json vint_doc = json(FIXTURE, &err);
        json_view vint_root = vint_doc.root();
        json_view vint_stars = vint_root.member("stars");
        json_view vint_name = vint_root.member("name");
        int vint_value = -1;
        int vint_got = vint_stars.get_int(&vint_value, &err);
        int vint_ok = vint_got == STDLIB_OK && err.ok() && vint_value == 5;
        int vint_bad = vint_name.get_int(&vint_value, &err);
        vint_ok = vint_ok && vint_bad == STDLIB_FAIL && err.failed_now() &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        REPORT("json_view::get_int", vint_ok);
        vint_doc.drop();
    }

    /* json_view::get_double: fixed 1.5 value. */
    {
        json vdbl_doc = json(FIXTURE, &err);
        json_view vdbl_root = vdbl_doc.root();
        json_view vdbl_ratio = vdbl_root.member("ratio");
        double vdbl_value = 0;
        int vdbl_got = vdbl_ratio.get_double(&vdbl_value, &err);
        REPORT("json_view::get_double",
               vdbl_got == STDLIB_OK && err.ok() && vdbl_value > 1.499 &&
                   vdbl_value < 1.501);
        vdbl_doc.drop();
    }

    /* json_view::get_bool: fixed true value. */
    {
        json vbool_doc = json(FIXTURE, &err);
        json_view vbool_root = vbool_doc.root();
        json_view vbool_flag = vbool_root.member("ok");
        int vbool_value = 0;
        int vbool_got = vbool_flag.get_bool(&vbool_value, &err);
        REPORT("json_view::get_bool",
               vbool_got == STDLIB_OK && err.ok() && vbool_value == 1);
        vbool_doc.drop();
    }

    /* json_view::get_string: fixed "cplus" value. */
    {
        json vstr_doc = json(FIXTURE, &err);
        json_view vstr_root = vstr_doc.root();
        json_view vstr_name = vstr_root.member("name");
        const char *vstr_value = NULL;
        int vstr_got = vstr_name.get_string(&vstr_value, &err);
        REPORT("json_view::get_string",
               vstr_got == STDLIB_OK && err.ok() && vstr_value != NULL &&
                   strcmp(vstr_value, "cplus") == 0);
        vstr_doc.drop();
    }

    /* json_view::size: array size 3, object size 6, scalar size 0. */
    {
        json vsize_doc = json(FIXTURE, &err);
        json_view vsize_root = vsize_doc.root();
        json_view vsize_tags = vsize_root.member("tags");
        json_view vsize_stars = vsize_root.member("stars");
        REPORT("json_view::size", vsize_tags.size() == 3 && vsize_root.size() == 6 &&
                                      vsize_stars.size() == 0);
        vsize_doc.drop();
    }

    /* json_view::at: index 0 -> "a", index 99 -> invalid. */
    {
        json vat_doc = json(FIXTURE, &err);
        json_view vat_root = vat_doc.root();
        json_view vat_tags = vat_root.member("tags");
        json_view vat_first = vat_tags.at(0);
        json_view vat_far = vat_tags.at(99);
        const char *vat_value = NULL;
        int vat_got = vat_first.get_string(&vat_value, &err);
        REPORT("json_view::at", vat_got == STDLIB_OK && vat_value != NULL &&
                                    strcmp(vat_value, "a") == 0 && !vat_far.valid());
        vat_doc.drop();
    }

    /* json_view::member: nested member + missing key. */
    {
        json vm_doc = json(FIXTURE, &err);
        json_view vm_root = vm_doc.root();
        json_view vm_nested = vm_root.member("nested");
        json_view vm_x = vm_nested.member("x");
        json_view vm_missing = vm_root.member("missing");
        int vm_value = -1;
        int vm_got = vm_x.get_int(&vm_value, &err);
        REPORT("json_view::member", vm_nested.valid() && vm_got == STDLIB_OK &&
                                        vm_value == 7 && !vm_missing.valid());
        vm_doc.drop();
    }

    /* json_view::write: serialises a sub-view to a fixed byte sequence. */
    {
        json vw_doc = json(FIXTURE, &err);
        json_view vw_root = vw_doc.root();
        json_view vw_tags = vw_root.member("tags");
        size_t vw_length = 0;
        char *vw_buffer = vw_tags.write(&vw_length);
        int vw_ok = vw_buffer != NULL && vw_length > 0 &&
                    strcmp(vw_buffer, "[\"a\",\"b\",\"c\"]") == 0;
        json_free(vw_buffer);
        REPORT("json_view::write", vw_ok);
        vw_doc.drop();
    }

    /* ---- json methods --------------------------------------------------- */

    /* json::json: valid text parses, malformed/NULL text yields invalid. */
    {
        json jctor_good = json(FIXTURE, &err);
        int jctor_good_ok = jctor_good.valid() && err.ok();
        json jctor_bad = json("{not json", &err);
        int jctor_bad_ok = !jctor_bad.valid() && err.failed_now();
        json jctor_none = json((const char *)0, &err);
        int jctor_ok = jctor_good_ok && jctor_bad_ok && !jctor_none.valid();
        REPORT("json::json", jctor_ok);
        jctor_good.drop();
        jctor_bad.drop();
        jctor_none.drop();
    }

    /* json::valid: mirrors document validity. */
    {
        json jvalid_good = json(FIXTURE, &err);
        json jvalid_bad = json("{", &err);
        REPORT("json::valid", jvalid_good.valid() && !jvalid_bad.valid());
        jvalid_good.drop();
        jvalid_bad.drop();
    }

    /* json::read_size: byte count after parse, zero for invalid document. */
    {
        json jread_good = json(FIXTURE, &err);
        json jread_empty = json((const char *)0, &err);
        REPORT("json::read_size", jread_good.read_size() == strlen(FIXTURE) &&
                                      jread_empty.read_size() == 0);
        jread_good.drop();
        jread_empty.drop();
    }

    /* json::value_count: counts values after parse, zero for invalid. */
    {
        json jcount_good = json(FIXTURE, &err);
        json jcount_empty = json((const char *)0, &err);
        REPORT("json::value_count",
               jcount_good.value_count() > 0 && jcount_empty.value_count() == 0);
        jcount_good.drop();
        jcount_empty.drop();
    }

    /* json::root: object root for valid doc, invalid view otherwise. */
    {
        json jroot_good = json(FIXTURE, &err);
        json jroot_empty = json((const char *)0, &err);
        json_view jroot_view = jroot_good.root();
        json_view jroot_none = jroot_empty.root();
        REPORT("json::root", jroot_view.valid() && jroot_view.is_object() &&
                                 !jroot_none.valid());
        jroot_good.drop();
        jroot_empty.drop();
    }

    /* json::drop: frees document; second drop is inert. */
    {
        json jdrop_doc = json(FIXTURE, &err);
        int jdrop_before = jdrop_doc.valid();
        jdrop_doc.drop();
        int jdrop_after = jdrop_doc.valid();
        jdrop_doc.drop();
        REPORT("json::drop", jdrop_before == 1 && jdrop_after == 0 &&
                                 !jdrop_doc.valid());
    }

    /* ---- json_builder methods ------------------------------------------- */

    /* json_builder::json_builder: fresh builder is valid. */
    {
        json_builder bctor_builder = json_builder();
        REPORT("json_builder::json_builder", bctor_builder.valid());
        bctor_builder.drop();
    }

    /* json_builder::valid: true before drop, false after (double drop safe). */
    {
        json_builder bvalid_builder = json_builder();
        int bvalid_before = bvalid_builder.valid();
        bvalid_builder.drop();
        int bvalid_after = bvalid_builder.valid();
        bvalid_builder.drop();
        REPORT("json_builder::valid", bvalid_before == 1 && bvalid_after == 0 &&
                                          !bvalid_builder.valid());
    }

    /* json_builder::add_int: OK, int appears in serialised document. */
    {
        json_builder bint_builder = json_builder();
        int bint_rc = bint_builder.add_int("year", 2026, &err);
        size_t bint_length = 0;
        char *bint_buffer = bint_builder.write(&bint_length);
        int bint_ok = bint_rc == STDLIB_OK && err.ok() && bint_buffer != NULL &&
                      strstr(bint_buffer, "\"year\":2026") != NULL;
        json_free(bint_buffer);
        REPORT("json_builder::add_int", bint_ok);
        bint_builder.drop();
    }

    /* json_builder::add_double: OK, finite value; NaN rejected. */
    {
        json_builder bdbl_builder = json_builder();
        int bdbl_rc = bdbl_builder.add_double("version", 1.5, &err);
        size_t bdbl_length = 0;
        char *bdbl_buffer = bdbl_builder.write(&bdbl_length);
        int bdbl_ok = bdbl_rc == STDLIB_OK && err.ok() && bdbl_buffer != NULL &&
                      strstr(bdbl_buffer, "\"version\":1.5") != NULL;
        json_free(bdbl_buffer);
        bdbl_builder.add_double("nan", 0.0 / 0.0, &err);
        bdbl_ok = bdbl_ok && err.failed_now() &&
                  err.category() == STDLIB_CATEGORY_RANGE;
        REPORT("json_builder::add_double", bdbl_ok);
        bdbl_builder.drop();
    }

    /* json_builder::add_bool: OK, true/false serialised. */
    {
        json_builder bbool_builder = json_builder();
        int bbool_rc1 = bbool_builder.add_bool("safe", 1, &err);
        int bbool_rc2 = bbool_builder.add_bool("off", 0, &err);
        size_t bbool_length = 0;
        char *bbool_buffer = bbool_builder.write(&bbool_length);
        int bbool_ok = bbool_rc1 == STDLIB_OK && bbool_rc2 == STDLIB_OK && err.ok() &&
                       bbool_buffer != NULL &&
                       strstr(bbool_buffer, "\"safe\":true") != NULL &&
                       strstr(bbool_buffer, "\"off\":false") != NULL;
        json_free(bbool_buffer);
        REPORT("json_builder::add_bool", bbool_ok);
        bbool_builder.drop();
    }

    /* json_builder::add_string: OK, value copied (source mutated after add). */
    {
        json_builder bstr_builder = json_builder();
        char bstr_source[16];
        strcpy(bstr_source, "cplus");
        int bstr_rc = bstr_builder.add_string("lang", bstr_source, &err);
        bstr_source[0] = 'X';
        size_t bstr_length = 0;
        char *bstr_buffer = bstr_builder.write(&bstr_length);
        int bstr_ok = bstr_rc == STDLIB_OK && err.ok() && bstr_buffer != NULL &&
                      strstr(bstr_buffer, "\"lang\":\"cplus\"") != NULL &&
                      strstr(bstr_buffer, "Xplus") == NULL;
        json_free(bstr_buffer);
        REPORT("json_builder::add_string", bstr_ok);
        bstr_builder.drop();
    }

    /* json_builder::write: all four added fields appear with a length. */
    {
        json_builder bw_builder = json_builder();
        bw_builder.add_string("lang", "cplus", &err);
        bw_builder.add_int("year", 2026, &err);
        bw_builder.add_double("ratio", 1.5, &err);
        bw_builder.add_bool("ok", 1, &err);
        size_t bw_length = 0;
        char *bw_buffer = bw_builder.write(&bw_length);
        int bw_ok = err.ok() && bw_buffer != NULL && bw_length > 0 &&
                    strstr(bw_buffer, "\"lang\":\"cplus\"") != NULL &&
                    strstr(bw_buffer, "\"year\":2026") != NULL &&
                    strstr(bw_buffer, "\"ratio\":1.5") != NULL &&
                    strstr(bw_buffer, "\"ok\":true") != NULL;
        json_free(bw_buffer);
        REPORT("json_builder::write", bw_ok);
        bw_builder.drop();
    }

    /* json_builder::drop: frees document; second drop is inert. */
    {
        json_builder bdrop_builder = json_builder();
        int bdrop_before = bdrop_builder.valid();
        bdrop_builder.drop();
        bdrop_builder.drop();
        REPORT("json_builder::drop", bdrop_before == 1 && !bdrop_builder.valid());
    }

    print("RESULT", "failures", failures);
    return failures != 0;
}

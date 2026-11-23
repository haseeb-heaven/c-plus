/* Adversarial regression family: NULL / zero / empty boundaries.
 *
 * Scope (all APIs verified to exist in this tree by reading the headers):
 *   error.hp       - error_reset/error_fail/error_fail_cat with NULL targets,
 *                    error lifecycle methods, repeated clear.
 *   io.hp          - read_line/read_int/read_long/read_float/read_double with
 *                    NULL out args; stdlib_io_token with NULL / zero size.
 *   file.hp        - file(NULL, NULL), file_borrow(NULL), closed-handle ops,
 *                    read/read_line NULL + zero length, write(NULL),
 *                    write_bytes(NULL, >0), repeated close.
 *   string.hp      - NULL constructor, append/equals/compare/find/copy_from
 *                    with NULL, substr/field NULL or zero out size, empty
 *                    string reads, string_view(NULL, 0), UTF-8 NULL rules.
 *   vector.hp      - NULL out args on at_checked/pop_back, empty container
 *                    refusals, out-of-range insert/erase, empty slice, stale
 *                    slice refusal (metadata only, no stale dereference).
 *   collections.hp - NULL out args on get/pop/top/front/at_checked, NULL keys,
 *                    empty-container refusals, repeated clear/drop.
 *   memory.hp      - NULL capacity/count/token/check/copy/move/set/borrow/
 *                    transfer/release, zero-length operations, untracked
 *                    pointers, stale generation tokens (metadata/refusal).
 *
 * Every check is deterministic and self-checking: it prints "<tag> <0|1>" and
 * accumulates counters.  Final line: "ADV_NULL_ZERO <checks> <failures>".
 * The process exits non-zero if and only if failures != 0.  No operation in
 * this file is expected to be fatal; no intentionally stale pointer is ever
 * dereferenced (staleness is detected through generation metadata).
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/memory.hp"

static int nz_checks = 0;
static int nz_failures = 0;

static void nz_check(const char *tag, int cond) {
    nz_checks++;
    if (!cond)
        nz_failures++;
    print(tag, cond ? 1 : 0);
}

/* Expect a failed error record with an exact category and exit code. */
static int nz_err_is(error *e, int failed, int category, int code) {
    int ok;
    if (!failed) {
        ok = e->failed_now() == 0 &&
             e->category() == STDLIB_CATEGORY_OK &&
             e->code() == (int)CPLUS_EXIT_OK && e->what()[0] == '\0';
    } else {
        ok = e->failed_now() == 1 && e->category() == category &&
             e->code() == code && e->what()[0] != '\0';
    }
    return ok;
}

static int nz_all_byte(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

/* ---- error.hp ----------------------------------------------------------- */

static void nz_error_api(void) {
    error e = error();

    nz_check("nz_err_default", nz_err_is(&e, 0, 0, 0));

    /* NULL targets are documented no-ops / telemetry-only reports. */
    error_reset((error *)0);
    error_fail((error *)0, CPLUS_EXIT_IO, "null-target io");
    error_fail_cat((error *)0, STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED, "null-target invalid");
    nz_check("nz_err_null_targets", nz_err_is(&e, 0, 0, 0));

    e.set(CPLUS_EXIT_IO, "io failure");
    nz_check("nz_err_set", nz_err_is(&e, 1, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO));

    e.set_category(STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS, "bounds failure");
    nz_check("nz_err_setcat",
             nz_err_is(&e, 1, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS));

    e.clear();
    nz_check("nz_err_clear", nz_err_is(&e, 0, 0, 0));
    e.clear();
    e.clear();
    nz_check("nz_err_repeat_clear", nz_err_is(&e, 0, 0, 0));
}

/* ---- io.hp -------------------------------------------------------------- */

static void nz_io_api(void) {
    error e = error();
    char buf[2];
    int i = 7;
    long l = 7;
    float f = 7;
    double d = 7;
    int st;
    int k;
    int j;

    buf[0] = 'S';
    buf[1] = 'T';

    st = read_line((char *)0, 8, &e);
    nz_check("nz_io_line_null",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_INVALID);
    nz_check("nz_io_line_null_sentinel", buf[0] == 'S' && buf[1] == 'T');

    st = read_line(buf, 0, &e);
    nz_check("nz_io_line_zero",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_INVALID);
    nz_check("nz_io_line_zero_sentinel", buf[0] == 'S' && buf[1] == 'T');

    /* Repeated zero-length calls are refused identically, no state leak. */
    k = 1;
    for (j = 0; j < 3; j++) {
        st = read_line(buf, 0, &e);
        k = k && (st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
                  e.category() == STDLIB_CATEGORY_INVALID &&
                  buf[0] == 'S' && buf[1] == 'T');
    }
    nz_check("nz_io_line_repeat_zero", k);

    st = read_int((int *)0, &e);
    nz_check("nz_io_int_null",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_INVALID);
    st = read_long((long *)0, &e);
    nz_check("nz_io_long_null",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_INVALID);
    st = read_float((float *)0, &e);
    nz_check("nz_io_float_null",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_INVALID);
    st = read_double((double *)0, &e);
    nz_check("nz_io_double_null",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_INVALID);
    nz_check("nz_io_out_unchanged", i == 7 && l == 7 && f == 7 && d == 7);

    /* stdlib_io_token refuses NULL / zero before touching stdin. */
    nz_check("nz_io_token_null", stdlib_io_token((char *)0, 8) == 1);
    nz_check("nz_io_token_zero", stdlib_io_token(buf, 0) == 1);
    nz_check("nz_io_token_sentinel", buf[0] == 'S' && buf[1] == 'T');
}

/* ---- file.hp ------------------------------------------------------------ */

static void nz_file_api(void) {
    error e = error();
    file closed = file((const char *)0, (const char *)0);
    file borrowed = file_borrow((FILE *)0);
    file f = file_temp(&e);
    char buf[8];
    int st;

    buf[0] = 'S';
    buf[1] = 'T';

    nz_check("nz_file_null_path", closed.is_open() == 0);
    nz_check("nz_file_borrow_null", borrowed.is_open() == 0);

    st = closed.read(buf, sizeof(buf), &e);
    nz_check("nz_file_closed_read",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_IO);
    st = closed.read_line(buf, sizeof(buf), &e);
    nz_check("nz_file_closed_readline",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_IO);
    st = closed.write_bytes(buf, 1, &e);
    nz_check("nz_file_closed_write",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO &&
             e.category() == STDLIB_CATEGORY_IO);
    st = closed.flush(&e);
    nz_check("nz_file_closed_flush",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO);
    st = closed.seek(0, SEEK_SET, &e);
    nz_check("nz_file_closed_seek",
             st == STDLIB_FAIL && e.code() == (int)CPLUS_EXIT_IO);
    st = closed.close(&e);
    nz_check("nz_file_close_closed",
             st == STDLIB_FAIL &&
             e.category() == STDLIB_CATEGORY_LIFECYCLE &&
             e.code() == (int)CPLUS_EXIT_IO);
    st = closed.close(&e);
    nz_check("nz_file_close_repeat",
             st == STDLIB_FAIL &&
             e.category() == STDLIB_CATEGORY_LIFECYCLE &&
             e.code() == (int)CPLUS_EXIT_IO);
    nz_check("nz_file_sentinel_after_closed", buf[0] == 'S' && buf[1] == 'T');

    nz_check("nz_file_temp_open", f.is_open() == 1);
    if (f.is_open()) {
        st = f.read((char *)0, 4, &e);
        nz_check("nz_file_read_null",
                 st == STDLIB_FAIL &&
                 e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                 e.category() == STDLIB_CATEGORY_INVALID);
        st = f.read(buf, 0, &e);
        nz_check("nz_file_read_zero",
                 st == STDLIB_FAIL &&
                 e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                 e.category() == STDLIB_CATEGORY_INVALID);
        st = f.read_line((char *)0, 4, &e);
        nz_check("nz_file_readline_null",
                 st == STDLIB_FAIL &&
                 e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                 e.category() == STDLIB_CATEGORY_INVALID);
        st = f.read_line(buf, 0, &e);
        nz_check("nz_file_readline_zero",
                 st == STDLIB_FAIL &&
                 e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                 e.category() == STDLIB_CATEGORY_INVALID);
        nz_check("nz_file_write_null", f.write((const char *)0) == 0);
        st = f.write_bytes((const char *)0, 4, &e);
        nz_check("nz_file_writebytes_null",
                 st == STDLIB_FAIL &&
                 e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                 e.category() == STDLIB_CATEGORY_INVALID);
        st = f.write_bytes(buf, 0, &e);
        nz_check("nz_file_writebytes_zero",
                 st == STDLIB_OK && nz_err_is(&e, 0, 0, 0));
        nz_check("nz_file_sentinel_after_null", buf[0] == 'S' && buf[1] == 'T');
        f.drop();
        f.drop();
        nz_check("nz_file_double_drop", f.is_open() == 0);
    }
}

/* ---- string.hp ---------------------------------------------------------- */

static void nz_string_api(void) {
    char out[8];
    string s = string((const char *)0);
    string_view nz_view = string_view((const char *)0, 0);
    string_view nz_view2 = string_view((const char *)0, 3);

    nz_check("nz_str_null_ctor",
             s.valid() == 1 && s.size() == 0 && s.capacity() == 1 &&
             s.c_str() != (const char *)0 && s.c_str()[0] == '\0' &&
             s.equals(""));
    nz_check("nz_str_at_empty", s.at(0) == '\0' && s.at(99) == '\0');
    nz_check("nz_str_append_null", s.append((const char *)0) == -1);
    nz_check("nz_str_append_empty", s.append("") == 0 && s.size() == 0);
    nz_check("nz_str_equals_null", s.equals((const char *)0) == 0);
    nz_check("nz_str_compare_null", s.compare((const char *)0) == 1);
    nz_check("nz_str_find_null", s.find((const char *)0) == -1);
    nz_check("nz_str_copy_null", s.copy_from((string *)0) == -1);
    nz_check("nz_str_reserve_zero", s.reserve(0) == 0);
    nz_check("nz_str_reserve_same", s.reserve(s.capacity()) == 0);

    out[0] = 'S';
    nz_check("nz_str_substr_null_out",
             s.substr(0, 0, (char *)0, sizeof(out)) == -1);
    nz_check("nz_str_substr_zero_size", s.substr(0, 0, out, 0) == -1);
    nz_check("nz_str_substr_start_oob",
             s.substr(1, 1, out, sizeof(out)) == -1 && out[0] == 'S');
    nz_check("nz_str_substr_empty",
             s.substr(0, 5, out, sizeof(out)) == 0 && out[0] == '\0');

    out[0] = 'S';
    nz_check("nz_str_field_null_out",
             s.field(',', 0, (char *)0, sizeof(out)) == -1);
    nz_check("nz_str_field_zero_size", s.field(',', 0, out, 0) == -1);
    nz_check("nz_str_field_index_oob",
             s.field(',', 1, out, sizeof(out)) == -1 && out[0] == 'S');
    nz_check("nz_str_field_empty",
             s.field(',', 0, out, sizeof(out)) == 0 && out[0] == '\0');

    nz_check("nz_str_utf8_empty", s.is_valid_utf8() == 1);
    nz_check("nz_str_utf8_null_zero",
             stdlib_utf8_validate((const char *)0, 0) == 1);
    nz_check("nz_str_utf8_null_nonzero",
             stdlib_utf8_validate((const char *)0, 1) == 0);

    nz_check("nz_str_view_null_empty",
             nz_view.size() == 0 && nz_view.equals("") == 1 &&
             nz_view.equals((const char *)0) == 0 &&
             nz_view.is_valid_utf8() == 1);
    nz_check("nz_str_view_null_nonzero",
             nz_view2.size() == 3 && nz_view2.equals("abc") == 0 &&
             nz_view2.is_valid_utf8() == 0);

    s.drop();
    s.drop();
    nz_check("nz_str_double_drop",
             s.valid() == 0 && s.size() == 0 && s.c_str()[0] == '\0');
}

/* ---- vector.hp ---------------------------------------------------------- */

static void nz_vector_api(void) {
    int out = 123;
    vector nz_vec = vector();
    slice nz_empty = slice((int *)0, 0);
    slice nz_live;
    slice nz_sub;

    nz_check("nz_vec_empty", nz_vec.size() == 0 && nz_vec.capacity() == 0);
    nz_check("nz_vec_reserve_zero",
             nz_vec.reserve(0) == 0 && nz_vec.capacity() == 0);
    nz_check("nz_vec_at_oob", nz_vec.at(0) == 0 && nz_vec.at(99) == 0);
    nz_check("nz_vec_at_checked_null_out",
             nz_vec.at_checked(0, (int *)0) == -1);
    nz_check("nz_vec_at_checked_empty",
             nz_vec.at_checked(0, &out) == -1 && out == 123);
    nz_check("nz_vec_pop_null", nz_vec.pop_back((int *)0) == -1);
    nz_check("nz_vec_pop_empty", nz_vec.pop_back(&out) == -1 && out == 123);
    nz_check("nz_vec_insert_oob",
             nz_vec.insert(1, 7) == -1 && nz_vec.size() == 0);
    nz_check("nz_vec_erase_empty", nz_vec.erase(0, &out) == -1 && out == 123);
    nz_check("nz_vec_copy_null",
             nz_vec.copy_from((vector *)0) == -1 && nz_vec.size() == 0);

    nz_check("nz_vec_slice_empty",
             nz_empty.size() == 0 && nz_empty.valid() == 0 &&
             nz_empty.stale() == 0);
    nz_check("nz_vec_slice_empty_at",
             nz_empty.at_checked(0, &out) == -1 && out == 123);
    nz_check("nz_vec_slice_empty_null_out",
             nz_empty.at_checked(0, (int *)0) == -1);
    nz_sub = nz_empty.sub(0, 2);
    nz_check("nz_vec_slice_empty_sub", nz_sub.size() == 0);

    nz_vec.push_back(10);
    nz_vec.push_back(20);
    nz_check("nz_vec_insert_end",
             nz_vec.insert(nz_vec.size(), 30) == 0 && nz_vec.size() == 3);
    nz_check("nz_vec_at_checked_ok",
             nz_vec.at_checked(2, &out) == 0 && out == 30);

    nz_live = nz_vec.view();
    nz_check("nz_vec_slice_live", nz_live.valid() == 1 && nz_live.size() == 3);
    nz_check("nz_vec_slice_live_null_out",
             nz_live.at_checked(0, (int *)0) == -1);

    nz_vec.push_back(40);
    nz_vec.push_back(50); /* forces a reallocation: the old slice goes stale */
    nz_check("nz_vec_slice_stale", nz_live.stale() == 1 && nz_live.valid() == 0);
    out = 777;
    nz_check("nz_vec_slice_stale_refusal",
             nz_live.at_checked(0, &out) == -1 && out == 777);
    nz_sub = nz_live.sub(0, 2);
    nz_check("nz_vec_slice_stale_sub", nz_sub.size() == 0 && nz_sub.valid() == 0);

    nz_vec.clear();
    nz_vec.clear();
    nz_check("nz_vec_repeat_clear", nz_vec.size() == 0);
    nz_check("nz_vec_pop_after_clear", nz_vec.pop_back(&out) == -1 && out == 777);
    nz_vec.drop();
    nz_vec.drop();
    nz_check("nz_vec_double_drop", nz_vec.size() == 0 && nz_vec.capacity() == 0);
}

/* ---- collections.hp ----------------------------------------------------- */

static void nz_collections_api(void) {
    int out = 55;
    intmap im = intmap();
    strmap sm = strmap();
    intset is = intset();
    deque dq = deque();
    list lst = list();
    stack nz_stack = stack();
    queue nz_queue = queue();

    nz_check("nz_im_empty",
             im.size() == 0 && im.valid() == 0 &&
             im.get(1, (int *)0) == -1 && im.contains(1) == 0 &&
             im.remove(1) == -1);
    nz_check("nz_im_get_null_unchanged",
             im.get(1, &out) == -1 && out == 55);
    im.put(1, 10);
    nz_check("nz_im_get", im.get(1, &out) == 0 && out == 10);
    nz_check("nz_im_get_null_after", im.get(1, (int *)0) == -1);
    im.clear();
    im.clear();
    nz_check("nz_im_repeat_clear", im.size() == 0 && im.contains(1) == 0);
    im.drop();
    im.drop();
    nz_check("nz_im_double_drop", im.size() == 0 && im.valid() == 0);

    nz_check("nz_sm_empty",
             sm.size() == 0 && sm.valid() == 0 &&
             sm.put((const char *)0, 1) == -1 &&
             sm.get((const char *)0, &out) == -1 &&
             sm.get("x", (int *)0) == -1 &&
             sm.contains((const char *)0) == 0 && sm.remove("x") == -1);
    sm.put("a", 1);
    out = 55;
    nz_check("nz_sm_null_get_after",
             sm.get((const char *)0, &out) == -1 && out == 55);
    nz_check("nz_sm_null_remove", sm.remove((const char *)0) == -1 &&
                                    sm.size() == 1 && sm.contains("a") == 1);
    sm.drop();
    sm.drop();
    nz_check("nz_sm_double_drop", sm.size() == 0 && sm.valid() == 0);

    nz_check("nz_is_empty",
             is.size() == 0 && is.valid() == 0 &&
             is.contains(1) == 0 && is.remove(1) == -1);
    is.insert(1);
    nz_check("nz_is_insert", is.insert(1) == 0 && is.contains(1) == 1 &&
                             is.size() == 1);
    is.drop();
    is.drop();
    nz_check("nz_is_double_drop", is.size() == 0 && is.valid() == 0);

    nz_check("nz_dq_empty",
             dq.size() == 0 && dq.capacity() == 0 &&
             dq.pop_front((int *)0) == -1 && dq.pop_back((int *)0) == -1 &&
             dq.pop_front(&out) == -1 &&
             dq.at_checked(0, (int *)0) == -1 &&
             dq.at_checked(0, &out) == -1 && out == 55);
    dq.push_back(7);
    nz_check("nz_dq_at", dq.at_checked(0, &out) == 0 && out == 7);
    nz_check("nz_dq_at_null", dq.at_checked(0, (int *)0) == -1);
    dq.clear();
    dq.clear();
    dq.drop();
    dq.drop();
    nz_check("nz_dq_clear_drop", dq.size() == 0 && dq.capacity() == 0);

    out = 55;
    nz_check("nz_list_empty",
             lst.size() == 0 && lst.pop_front((int *)0) == -1 &&
             lst.pop_back((int *)0) == -1 && lst.pop_front(&out) == -1 &&
             lst.at_checked(0, (int *)0) == -1 &&
             lst.at_checked(0, &out) == -1 && out == 55);
    lst.push_back(9);
    nz_check("nz_list_at",
             lst.at_checked(0, (int *)0) == -1 &&
             lst.at_checked(0, &out) == 0 && out == 9);
    lst.drop();
    lst.drop();
    nz_check("nz_list_double_drop", lst.size() == 0);

    out = 55;
    nz_check("nz_stack_empty",
             nz_stack.size() == 0 && nz_stack.pop((int *)0) == -1 &&
             nz_stack.pop(&out) == -1 && nz_stack.top_checked((int *)0) == -1 &&
             nz_stack.top_checked(&out) == -1 && out == 55);
    nz_stack.push(3);
    nz_check("nz_stack_top",
             nz_stack.top_checked((int *)0) == -1 &&
             nz_stack.top_checked(&out) == 0 && out == 3);
    nz_stack.drop();
    nz_stack.drop();
    nz_check("nz_stack_double_drop", nz_stack.size() == 0);

    out = 55;
    nz_check("nz_queue_empty",
             nz_queue.size() == 0 && nz_queue.pop((int *)0) == -1 &&
             nz_queue.pop(&out) == -1 &&
             nz_queue.front_checked((int *)0) == -1 &&
             nz_queue.front_checked(&out) == -1 && out == 55);
    nz_queue.push(4);
    nz_check("nz_queue_front",
             nz_queue.front_checked((int *)0) == -1 &&
             nz_queue.front_checked(&out) == 0 && out == 4);
    nz_queue.drop();
    nz_queue.drop();
    nz_check("nz_queue_double_drop", nz_queue.size() == 0);
}

/* ---- memory.hp ---------------------------------------------------------- */

static void nz_memory_api(void) {
    error e = error();
    size_t cnt = 999;
    void *hold = (void *)0;
    unsigned char *p;
    void *nz_q;
    unsigned long long tok;
    int rc;

    nz_check("nz_mem_cap_null", stdlib_mem_capacity((void *)0) == 0);
    nz_check("nz_mem_count_null",
             stdlib_mem_count((void *)0, 1, &cnt) == -1 && cnt == 0);
    nz_check("nz_mem_token_null", stdlib_mem_token((void *)0) == 0);
    nz_check("nz_mem_check_null",
             stdlib_mem_check((void *)0, 0, &e) == STDLIB_FAIL &&
             e.category() == STDLIB_CATEGORY_INVALID &&
             e.code() == (int)CPLUS_EXIT_TYPE_CONFUSION);
    nz_check("nz_mem_range_null",
             stdlib_mem_check_range((void *)0, 0, 0, 1, &e) == STDLIB_FAIL &&
             e.category() == STDLIB_CATEGORY_INVALID &&
             e.code() == (int)CPLUS_EXIT_NULL_POINTER);
    nz_check("nz_mem_copy_null",
             stdlib_mem_copy((void *)0, 0, (void *)0, 0, 0, 0, &e) ==
                 STDLIB_FAIL &&
             e.code() == (int)CPLUS_EXIT_NULL_POINTER);
    nz_check("nz_mem_move_null_src",
             stdlib_mem_move((void *)0, 0, (void *)0, 0, 0, 0, &e) ==
                 STDLIB_FAIL &&
             e.code() == (int)CPLUS_EXIT_NULL_POINTER);
    nz_check("nz_mem_set_null",
             stdlib_mem_set((void *)0, 0, 0x41, 1, 0, &e) == STDLIB_FAIL &&
             e.code() == (int)CPLUS_EXIT_NULL_POINTER);
    nz_check("nz_mem_borrow_null",
             stdlib_mem_borrow((void *)0, 0, 0, 0, &e) == STDLIB_FAIL &&
             e.category() == STDLIB_CATEGORY_INVALID);
    nz_check("nz_mem_transfer_null",
             stdlib_mem_transfer((void **)0, &hold, &e) == STDLIB_FAIL &&
             e.category() == STDLIB_CATEGORY_OWNERSHIP);
    stdlib_mem_release((void **)0);
    stdlib_mem_release(&hold);
    nz_check("nz_mem_release_null", hold == (void *)0);

    p = (unsigned char *)stdlib_alloc(8);
    if (p == (unsigned char *)0) {
        nz_check("nz_mem_alloc", 0);
        return;
    }
    nz_check("nz_mem_alloc", 1);
    {
        size_t i;
        for (i = 0; i < 8; i++)
            p[i] = 0x5A;
    }
    tok = stdlib_mem_token(p);
    nz_check("nz_mem_token_nonzero", tok != 0);

    /* Zero-length operations at the exact end of the block are valid no-ops.
     * Guarded helpers do not reset `error`, so clear it first and confirm the
     * successful call leaves it clear. */
    e.clear();
    rc = stdlib_mem_set(p, 8, 0x00, 0, tok, &e);
    nz_check("nz_mem_zero_at_end",
             rc == STDLIB_OK && nz_err_is(&e, 0, 0, 0) &&
             nz_all_byte(p, 8, 0x5A));
    rc = stdlib_mem_copy(p, 0, p, 0, 0, tok, &e);
    nz_check("nz_mem_zero_copy",
             rc == STDLIB_OK && nz_all_byte(p, 8, 0x5A));
    rc = stdlib_mem_move(p, 0, p, 0, 0, tok, &e);
    nz_check("nz_mem_zero_move",
             rc == STDLIB_OK && nz_all_byte(p, 8, 0x5A));
    rc = stdlib_mem_borrow(p, 8, 0, tok, &e);
    nz_check("nz_mem_borrow_zero_at_end", rc == STDLIB_OK);

    /* Offset beyond capacity is refused; sentinel bytes stay untouched. */
    rc = stdlib_mem_set(p, 9, 0x00, 0, tok, &e);
    nz_check("nz_mem_off_gt_cap",
             rc == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_BOUNDS &&
             e.code() == (int)CPLUS_EXIT_BOUNDS && nz_all_byte(p, 8, 0x5A));

    /* Resizing bumps the generation; the OLD token is refused by metadata.
     * The possibly-moved old pointer is never read again. */
    nz_q = stdlib_realloc(p, 16);
    if (nz_q != (void *)0) {
        nz_check("nz_mem_stale_token",
                 stdlib_mem_check_token(nz_q, tok, &e) == STDLIB_FAIL &&
                 e.category() == STDLIB_CATEGORY_LIFECYCLE &&
                 e.code() == (int)CPLUS_EXIT_USE_AFTER_FREE);
        nz_check("nz_mem_fresh_token",
                 stdlib_mem_check(nz_q, stdlib_mem_token(nz_q), &e) ==
                     STDLIB_OK);
        nz_check("nz_mem_cap_after_realloc", stdlib_mem_capacity(nz_q) == 16);
        stdlib_mem_release(&nz_q);
    } else {
        nz_check("nz_mem_realloc", 0);
        {
            void *nz_old = p;
            stdlib_mem_release(&nz_old);
        }
    }
    nz_check("nz_mem_release_idempotent", nz_q == (void *)0);

    /* realloc(NULL, n) allocates; realloc(ptr, 0) maps to one backing byte. */
    nz_q = stdlib_realloc((void *)0, 3);
    nz_check("nz_mem_realloc_null", nz_q != (void *)0 &&
                                    stdlib_mem_capacity(nz_q) == 3);
    if (nz_q != (void *)0) {
        void *r = stdlib_realloc(nz_q, 0);
        nz_check("nz_mem_realloc_zero", r != (void *)0 &&
                                        stdlib_mem_capacity(r) == 1);
        if (r != (void *)0)
            nz_q = r;
        stdlib_mem_release(&nz_q);
    }
    /* An untracked (stack) pointer is refused, not dereferenced. */
    {
        char stackbuf[4] = {0, 0, 0, 0};
        /* stdlib_realloc() reports untracked pointers through stdlib_note()
         * (telemetry/callback), not through the error object; the refusal is
         * observed as a NULL return with no metadata created. */
        nz_check("nz_mem_untracked",
                 stdlib_realloc(stackbuf, 8) == (void *)0 &&
                 stdlib_mem_capacity(stackbuf) == 0);
    }
}

int main(void) {
    nz_error_api();
    nz_io_api();
    nz_file_api();
    nz_string_api();
    nz_vector_api();
    nz_collections_api();
    nz_memory_api();
    print("ADV_NULL_ZERO", nz_checks, nz_failures);
    return nz_failures == 0 ? 0 : 1;
}

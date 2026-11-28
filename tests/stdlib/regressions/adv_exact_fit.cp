/* Adversarial regression family: exact-fit / one-byte-too-small boundaries.
 *
 * Covers, with deterministic self-checking assertions:
 *   memory.hp  - guarded copy/set at exactly the tracked capacity and one byte
 *                past it, for capacities 1, 2, 3 and 4; sentinel preservation
 *                on every refusal; exact return/error codes.
 *   string.hp  - substr()/field() into exact-fit, one-byte-too-small (truncated
 *                by contract), and one-byte output buffers; 1..4 byte sweeps;
 *                empty input; repeated zero-length calls; stale sentinels.
 *   file.hp    - read_line() exact fit, one-byte-too-small, and one-byte buffer
 *                over a temp file with controlled contents; stream alignment.
 *   vector.hp  - exact capacity fill, checked slice reads at all indices,
 *                first overflow push, stale-slice refusal by metadata.
 *   collections.hp - deque/stack/queue exact-capacity fill, one-more push,
 *                checked empty refusals (no out-of-bounds reads).
 *
 * No intentionally stale pointer is ever dereferenced: stale slice reads are
 * refused through generation metadata before any memory access.
 *
 * Each check prints "<tag> <0|1>".  Final line: "ADV_EXACT_FIT <checks>
 * <failures>".  Exit non-zero iff failures != 0.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/memory.hp"

static int ef_checks = 0;
static int ef_failures = 0;

static void ef_check(const char *tag, int cond) {
    ef_checks++;
    if (!cond)
        ef_failures++;
    print(tag, cond ? 1 : 0);
}

static int ef_err_is(error *e, int failed, int category, int code) {
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

static int ef_all_byte(const unsigned char *p, size_t n, unsigned char v) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

static int ef_bytes_eq(const unsigned char *a, const unsigned char *b,
                       size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

/* ---- memory.hp: 1..4 byte capacity sweeps -------------------------------- */

static void ef_mem_case(const char *prefix, size_t cap) {
    error e = error();
    char tag[48];
    unsigned char pattern[5];
    unsigned char *ef_src;
    unsigned char *ef_dst;
    void *ef_src_h;
    void *ef_dst_h;
    unsigned long long tok;
    int rc;
    size_t i;

    pattern[0] = 0x11;
    pattern[1] = 0x22;
    pattern[2] = 0x33;
    pattern[3] = 0x44;
    pattern[4] = 0x55;

    ef_src = (unsigned char *)stdlib_alloc(cap + 1);
    ef_dst = (unsigned char *)stdlib_alloc(cap);
    if (ef_src == (unsigned char *)0 || ef_dst == (unsigned char *)0) {
        snprintf(tag, sizeof(tag), "%s_alloc", prefix);
        ef_check(tag, 0);
        ef_src_h = ef_src;
        ef_dst_h = ef_dst;
        stdlib_mem_release(&ef_src_h);
        stdlib_mem_release(&ef_dst_h);
        return;
    }
    for (i = 0; i < cap + 1; i++)
        ef_src[i] = pattern[i];
    for (i = 0; i < cap; i++)
        ef_dst[i] = 0xEE;
    tok = stdlib_mem_token(ef_dst);

    snprintf(tag, sizeof(tag), "%s_cap", prefix);
    ef_check(tag, stdlib_mem_capacity(ef_dst) == cap);

    /* Exact fit: n == destination capacity. */
    snprintf(tag, sizeof(tag), "%s_copy_exact", prefix);
    rc = stdlib_mem_copy(ef_dst, 0, ef_src, 0, cap, tok, &e);
    ef_check(tag, rc == STDLIB_OK && ef_err_is(&e, 0, 0, 0) &&
                      ef_bytes_eq(ef_dst, ef_src, cap));

    /* One byte too many: refused with BOUNDS, destination untouched. */
    for (i = 0; i < cap; i++)
        ef_dst[i] = 0xEE;
    snprintf(tag, sizeof(tag), "%s_copy_over", prefix);
    rc = stdlib_mem_copy(ef_dst, 0, ef_src, 0, cap + 1, tok, &e);
    ef_check(tag, rc == STDLIB_FAIL &&
                      e.category() == STDLIB_CATEGORY_BOUNDS &&
                      e.code() == (int)CPLUS_EXIT_BOUNDS &&
                      ef_all_byte(ef_dst, cap, 0xEE));

    /* Exact-fit fill.  Clear the error left by the previous refusal first:
     * guarded helpers report failures but never reset the error object. */
    for (i = 0; i < cap; i++)
        ef_dst[i] = 0xEE;
    e.clear();
    snprintf(tag, sizeof(tag), "%s_set_exact", prefix);
    rc = stdlib_mem_set(ef_dst, 0, 0x77, cap, tok, &e);
    ef_check(tag, rc == STDLIB_OK && ef_err_is(&e, 0, 0, 0) &&
                      ef_all_byte(ef_dst, cap, 0x77));

    /* One-too-many fill: refused, sentinel preserved. */
    for (i = 0; i < cap; i++)
        ef_dst[i] = 0xEE;
    snprintf(tag, sizeof(tag), "%s_set_over", prefix);
    rc = stdlib_mem_set(ef_dst, 0, 0x77, cap + 1, tok, &e);
    ef_check(tag, rc == STDLIB_FAIL &&
                      e.category() == STDLIB_CATEGORY_BOUNDS &&
                      e.code() == (int)CPLUS_EXIT_BOUNDS &&
                      ef_all_byte(ef_dst, cap, 0xEE));

    /* Off-by-one interior offset: cap-1 bytes fit exactly at offset 1. */
    for (i = 0; i < cap; i++)
        ef_dst[i] = 0xEE;
    snprintf(tag, sizeof(tag), "%s_interior_exact", prefix);
    rc = stdlib_mem_set(ef_dst, 1, 0x33, cap - 1, tok, &e);
    ef_check(tag, rc == STDLIB_OK && ef_dst[0] == 0xEE &&
                      ef_all_byte(ef_dst + 1, cap - 1, 0x33));

    ef_src_h = ef_src;
    ef_dst_h = ef_dst;
    stdlib_mem_release(&ef_src_h);
    stdlib_mem_release(&ef_dst_h);
}

/* ---- string.hp: substr()/field() output buffer sweeps -------------------- */

static void ef_str_case(const char *prefix, const char *text, size_t n) {
    char tag[48];
    char out[8];
    string ef_s = string(text);
    int rc;
    int k;
    size_t i;

    snprintf(tag, sizeof(tag), "%s_size", prefix);
    ef_check(tag, ef_s.size() == n);

    /* Exact fit: n characters plus terminator in n+1 bytes. */
    for (i = 0; i < sizeof(out); i++)
        out[i] = 'Z';
    rc = ef_s.substr(0, n, out, n + 1);
    snprintf(tag, sizeof(tag), "%s_sub_exact", prefix);
    ef_check(tag, rc == 0 && strncmp(out, text, n) == 0 && out[n] == '\0' &&
                      out[n + 1] == 'Z');

    /* One-byte destination: truncates to zero characters, sentinel kept. */
    for (i = 0; i < sizeof(out); i++)
        out[i] = 'Z';
    rc = ef_s.substr(0, n, out, 1);
    snprintf(tag, sizeof(tag), "%s_sub_onebyte", prefix);
    ef_check(tag, rc == 0 && out[0] == '\0' && out[1] == 'Z');

    /* Repeated zero-length copies are stable no-ops. */
    k = 1;
    for (i = 0; i < 3; i++) {
        out[0] = 'Z';
        out[1] = 'Z';
        rc = ef_s.substr(0, 0, out, 1);
        k = k && rc == 0 && out[0] == '\0' && out[1] == 'Z';
    }
    snprintf(tag, sizeof(tag), "%s_sub_zero_repeat", prefix);
    ef_check(tag, k);

    if (n > 0) {
        /* One byte too small: the header truncates to out_size-1 characters
         * and still terminates; the byte past the buffer is untouched. */
        for (i = 0; i < sizeof(out); i++)
            out[i] = 'Z';
        rc = ef_s.substr(0, n, out, n);
        snprintf(tag, sizeof(tag), "%s_sub_short", prefix);
        ef_check(tag, rc == 0 && strncmp(out, text, n - 1) == 0 &&
                          out[n - 1] == '\0' && out[n] == 'Z');

        /* field() on a separator-free string mirrors substr(). */
        for (i = 0; i < sizeof(out); i++)
            out[i] = 'Z';
        rc = ef_s.field(',', 0, out, n + 1);
        snprintf(tag, sizeof(tag), "%s_field_exact", prefix);
        ef_check(tag, rc == 0 && strncmp(out, text, n) == 0 &&
                          out[n] == '\0' && out[n + 1] == 'Z');

        for (i = 0; i < sizeof(out); i++)
            out[i] = 'Z';
        rc = ef_s.field(',', 0, out, n);
        snprintf(tag, sizeof(tag), "%s_field_short", prefix);
        ef_check(tag, rc == 0 && strncmp(out, text, n - 1) == 0 &&
                          out[n - 1] == '\0' && out[n] == 'Z');
    } else {
        /* Empty input with a zero-sized destination is a refusal, and the
         * sentinel is preserved. */
        out[0] = 'Z';
        out[1] = 'Z';
        rc = ef_s.substr(0, 0, out, 0);
        snprintf(tag, sizeof(tag), "%s_sub_zero_refusal", prefix);
        ef_check(tag, rc == -1 && out[0] == 'Z' && out[1] == 'Z');

        out[0] = 'Z';
        out[1] = 'Z';
        rc = ef_s.field(',', 0, out, 0);
        snprintf(tag, sizeof(tag), "%s_field_zero_refusal", prefix);
        ef_check(tag, rc == -1 && out[0] == 'Z' && out[1] == 'Z');
    }

    ef_s.drop();
}

/* ---- file.hp: read_line() buffer boundaries over a controlled temp file -- */

static void ef_file_lines(void) {
    error e = error();
    file ef_f = file_temp(&e);
    const char *data = "ab\nabc\nabcd\nabcd\nz\n";
    char buf[8];
    int st;
    size_t i;

    ef_check("ef_file_open", ef_f.is_open() == 1);
    if (!ef_f.is_open())
        return;

    st = ef_f.write_bytes(data, strlen(data), &e);
    ef_check("ef_file_write", st == STDLIB_OK);
    st = ef_f.seek(0, SEEK_SET, &e);
    ef_check("ef_file_seek", st == STDLIB_OK);

    /* One-byte destination: a non-empty line is refused with BOUNDS, only the
     * terminator is written, the one-past sentinel is intact, and the line is
     * drained so the stream stays aligned. */
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 'Z';
    st = ef_f.read_line(buf, 1, &e);
    ef_check("ef_file_1byte_over",
             st == STDLIB_FAIL && e.category() == STDLIB_CATEGORY_BOUNDS &&
             e.code() == (int)CPLUS_EXIT_BOUNDS && buf[0] == '\0' &&
             buf[1] == 'Z');

    /* Next line "abc" fits exactly in a 4-byte destination. */
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 'Z';
    st = ef_f.read_line(buf, 4, &e);
    ef_check("ef_file_exact3", st == STDLIB_OK && strcmp(buf, "abc") == 0 &&
                                   ef_err_is(&e, 0, 0, 0) && buf[4] == 'Z');

    /* "abcd" fits exactly in 5 bytes. */
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 'Z';
    st = ef_f.read_line(buf, 5, &e);
    ef_check("ef_file_exact4", st == STDLIB_OK && strcmp(buf, "abcd") == 0);

    /* The next "abcd" into 4 bytes is one byte too small: refused with BOUNDS. */
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 'Z';
    st = ef_f.read_line(buf, 4, &e);
    ef_check("ef_file_short4_st", st == STDLIB_FAIL);
    ef_check("ef_file_short4_cat", e.category() == STDLIB_CATEGORY_BOUNDS);
    ef_check("ef_file_short4_code", e.code() == (int)CPLUS_EXIT_BOUNDS);
    ef_check("ef_file_short4_buf0", buf[0] == '\0');
    /* fgets() stores the truncated prefix inside the 4-byte destination before
     * the library refuses it; the byte one past the destination is untouched. */
    ef_check("ef_file_short4_sentinel", buf[4] == 'Z');

    /* The stream is still aligned: the short line was drained. */
    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 'Z';
    st = ef_f.read_line(buf, 2, &e);
    ef_check("ef_file_aligned", st == STDLIB_OK && strcmp(buf, "z") == 0);

    /* Repeated zero-length reads are refused identically, no state leak. */
    {
        int k = 1;
        for (i = 0; i < 3; i++) {
            st = ef_f.read_line(buf, 0, &e);
            k = k && st == STDLIB_FAIL &&
                e.category() == STDLIB_CATEGORY_INVALID &&
                e.code() == (int)CPLUS_EXIT_UNSUPPORTED;
        }
        ef_check("ef_file_zero_repeat", k);
    }

    /* EOF is clean after the boundary sequence. */
    st = ef_f.read_line(buf, sizeof(buf), &e);
    ef_check("ef_file_eof", st == STDLIB_EOF && ef_err_is(&e, 0, 0, 0));

    ef_f.drop();
}

/* ---- vector.hp: exact capacity and checked slice reads ------------------- */

static void ef_vector_fit(void) {
    int out = -1;
    vector ef_vec = vector();
    slice ef_sl;
    int i;
    int k;

    ef_check("ef_vec_reserve4",
             ef_vec.reserve(4) == 0 && ef_vec.capacity() == 4);
    ef_check("ef_vec_reserve_repeat",
             ef_vec.reserve(4) == 0 && ef_vec.capacity() == 4);

    k = 1;
    for (i = 0; i < 4; i++)
        k = k && ef_vec.push_back(i * 10) == 0;
    ef_check("ef_vec_fill_exact",
             k && ef_vec.size() == 4 && ef_vec.capacity() == 4);

    ef_sl = ef_vec.view();
    ef_check("ef_vec_view_exact", ef_sl.valid() == 1 && ef_sl.size() == 4);

    k = 1;
    for (i = 0; i < 4; i++) {
        out = -1;
        k = k && ef_sl.at_checked((size_t)i, &out) == 0 && out == i * 10;
    }
    ef_check("ef_vec_view_values", k);

    out = -1;
    ef_check("ef_vec_view_oob", ef_sl.at_checked(4, &out) == -1 && out == -1);

    ef_check("ef_vec_push_grow",
             ef_vec.push_back(99) == 0 && ef_vec.size() == 5 &&
             ef_vec.capacity() == 8);
    ef_check("ef_vec_view_stale", ef_sl.stale() == 1 && ef_sl.valid() == 0);
    out = -1;
    ef_check("ef_vec_stale_refusal", ef_sl.at_checked(0, &out) == -1 &&
                                         out == -1);

    out = -1;
    ef_check("ef_vec_pop_exact", ef_vec.pop_back(&out) == 0 && out == 99 &&
                                     ef_vec.size() == 4);
    ef_check("ef_vec_shrink", ef_vec.shrink_to_fit() == 0 &&
                                  ef_vec.capacity() == 4 && ef_vec.size() == 4);
    ef_vec.drop();
    ef_vec.drop();
    ef_check("ef_vec_double_drop", ef_vec.size() == 0 && ef_vec.capacity() == 0);
}

/* ---- collections.hp: ring/size boundaries -------------------------------- */

static void ef_ring_fit(void) {
    deque ef_dq = deque();
    stack ef_st = stack();
    queue ef_qu = queue();
    int out = -1;
    int i;
    int k;
    int ok;

    k = 1;
    for (i = 0; i < 8; i++)
        k = k && ef_dq.push_back(i) == 0;
    ef_check("ef_dq_exact", k && ef_dq.size() == 8 && ef_dq.capacity() == 8);
    ef_check("ef_dq_push_over",
             ef_dq.push_back(8) == 0 && ef_dq.size() == 9 &&
             ef_dq.capacity() == 16);
    k = 1;
    for (i = 0; i < 9; i++) {
        out = -1;
        k = k && ef_dq.pop_front(&out) == 0 && out == i;
    }
    ef_check("ef_dq_drain", k && ef_dq.size() == 0);
    ef_check("ef_dq_empty_refusal", ef_dq.pop_front(&out) == -1 &&
                                        ef_dq.at_checked(0, &out) == -1);
    ef_dq.drop();

    k = 1;
    for (i = 0; i < 8; i++)
        k = k && ef_st.push(i) == 0;
    ef_check("ef_st_exact", k && ef_st.size() == 8);
    ok = 1;
    for (i = 7; i >= 0; i--) {
        out = -1;
        ok = ok && ef_st.pop(&out) == 0 && out == i;
    }
    ef_check("ef_st_drain", ok && ef_st.size() == 0);
    ef_check("ef_st_empty_refusal", ef_st.pop(&out) == -1 &&
                                        ef_st.top_checked(&out) == -1);
    ef_st.drop();

    k = 1;
    for (i = 0; i < 8; i++)
        k = k && ef_qu.push(i) == 0;
    ef_check("ef_qu_exact", k && ef_qu.size() == 8);
    out = -1;
    ef_check("ef_qu_front", ef_qu.front_checked(&out) == 0 && out == 0);
    ok = 1;
    for (i = 0; i < 8; i++) {
        out = -1;
        ok = ok && ef_qu.pop(&out) == 0 && out == i;
    }
    ef_check("ef_qu_drain", ok && ef_qu.size() == 0);
    ef_check("ef_qu_empty_refusal", ef_qu.pop(&out) == -1 &&
                                        ef_qu.front_checked(&out) == -1);
    ef_qu.drop();
}

int main(void) {
    ef_mem_case("ef_m1", 1);
    ef_mem_case("ef_m2", 2);
    ef_mem_case("ef_m3", 3);
    ef_mem_case("ef_m4", 4);

    ef_str_case("ef_s0", "", 0);
    ef_str_case("ef_s1", "a", 1);
    ef_str_case("ef_s2", "ab", 2);
    ef_str_case("ef_s3", "abc", 3);
    ef_str_case("ef_s4", "abcd", 4);

    ef_file_lines();
    ef_vector_fit();
    ef_ring_fit();

    print("ADV_EXACT_FIT", ef_checks, ef_failures);
    return ef_failures == 0 ? 0 : 1;
}

/* Hardening regressions for io, net, log, time, and json modules. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/json.hp"
#include "stdlib/log.hp"
#include "stdlib/net.hp"
#include "stdlib/string.hp"
#include "stdlib/time.hp"
#include "stdlib/vector.hp"

int main(void) {
    error err = error();

    /* 1. io.hp: size==1 read_line must not underflow; overlong token must fail. */
    char tiny_buf[2];
    tiny_buf[0] = 'x';
    tiny_buf[1] = '\0';
    int r1 = read_line(tiny_buf, 1, &err);
    int r2 = read_line(tiny_buf, 1, &err);
    int r2_cat = err.category();
    int tok_val = 0;
    int r3 = read_int(&tok_val, &err);
    int r3_cat = err.category();
    int r4 = read_int(&tok_val, &err);
    int r5 = read_int(NULL, &err);
    int r5_cat = err.category();
    print("io", r1 == STDLIB_OK && tiny_buf[0] == '\0',
          r2 == STDLIB_FAIL && r2_cat == STDLIB_CATEGORY_BOUNDS,
          r3 == STDLIB_FAIL && r3_cat == STDLIB_CATEGORY_RANGE,
          r4 == STDLIB_OK && tok_val == 42,
          r5 == STDLIB_FAIL && r5_cat == STDLIB_CATEGORY_INVALID);

    /* 2. net.hp: invalid ports and invalid handle I/O checks. */
    listener bad_neg = listener(-1);
    listener bad_high = listener(70000);
    connection bad_conn = connection(-1);
    char netbuf[8];
    int nr = bad_conn.read(netbuf, sizeof(netbuf), &err);
    int nr_cat = err.category();
    int nw = bad_conn.write("hi", 2, &err);
    int nw_cat = err.category();
    print("net", !bad_neg.is_valid(), !bad_high.is_valid(),
          nr == STDLIB_FAIL && nr_cat == STDLIB_CATEGORY_INVALID,
          nw == STDLIB_FAIL && nw_cat == STDLIB_CATEGORY_INVALID);

    /* 3. log.hp: NULL-stream owned sink must stay safe; line truncation errors. */
    log_sink null_owned = log_sink(NULL, 1, LOG_INFO);
    null_owned.drop();
    log_sink devnull = log_open_file("/dev/null", LOG_INFO, &err);
    char long_msg[600];
    memset(long_msg, 'A', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    int lw = devnull.write(LOG_INFO, long_msg, NULL, NULL, 0, "f.cp", 1, &err);
    print("log", devnull.valid(), lw == STDLIB_FAIL,
          err.category() == STDLIB_CATEGORY_BOUNDS);

    /* 4. time.hp: null buffer, max uint64 timestamp in valid buffer, tiny buffer errors. */
    char tbuf[4];
    char full_tbuf[32];
    int tnull = time_format_iso8601(1000000000ULL, NULL, 32, &err);
    int tnull_cat = err.category();
    int tmax = time_format_iso8601(18446744073709551615ULL, full_tbuf, sizeof(full_tbuf), &err);
    int tsmall = time_format_iso8601(1000000000ULL, tbuf, sizeof(tbuf), &err);
    int tsmall_cat = err.category();
    print("time", tnull == STDLIB_FAIL && tnull_cat == STDLIB_CATEGORY_INVALID && tmax == STDLIB_OK,
          tsmall == STDLIB_FAIL && tsmall_cat == STDLIB_CATEGORY_BOUNDS);

    /* 5. json.hp: float/overflow rejected by get_int; NULL key & NAN rejected. */
    json doc = json("{\"f\":1.5,\"big\":9999999999999,\"ok\":7}", &err);
    json_view root = doc.root();
    int out_i = 0;
    json_view vf = root.member("f");
    json_view vb = root.member("big");
    json_view vok = root.member("ok");
    int jf = vf.get_int(&out_i, &err);
    int jf_cat = err.category();
    int jb = vb.get_int(&out_i, &err);
    int jb_cat = err.category();
    int jok = vok.get_int(&out_i, &err);
    json_builder b = json_builder();
    int bnull = b.add_int(NULL, 1, &err);
    int bnull_cat = err.category();
    int bnan = b.add_double("x", NAN, &err);
    int bnan_cat = err.category();
    print("json", jf == STDLIB_FAIL && jf_cat == STDLIB_CATEGORY_INVALID,
          jb == STDLIB_FAIL && jb_cat == STDLIB_CATEGORY_RANGE,
          jok == STDLIB_OK && out_i == 7,
          bnull == STDLIB_FAIL && bnull_cat == STDLIB_CATEGORY_INVALID,
          bnan == STDLIB_FAIL && bnan_cat == STDLIB_CATEGORY_RANGE);

    /* 6. Direct constructors and view helpers for 100% API surface coverage */
    json_view empty_jv = json_view(NULL);
    json_view wrapped_jv = json_view_of(NULL);
    string_view sv_direct = string_view("abc", 3);
    slice sl_direct = slice(NULL, 0);
    if (empty_jv.valid() || wrapped_jv.valid() ||
        sv_direct.size() != 3 || sl_direct.size() != 0)
        return 2;
    return 0;
}

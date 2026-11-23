/* Task 3: stdlib/error.hp as the error propagation hub - SYSTEM APIs.
 *
 * Self-checking, deterministic, single translation unit. Every check prints
 * "<tag> <0|1>"; the final line is "ERROR_HUB_SYSTEM <checks> <failures>".
 * Exit 0 only when failures == 0. No stdin, no clock, no randomness, no
 * external peer and no fatal probe is used. Every loop is bounded and every
 * input is a fixed literal (fixed seed = fully deterministic).
 *
 * Coverage:
 *   P1  each system family (io / file / fs / json / net / thread) writes its
 *       documented category + code into the shared `error` value;
 *   P2  the requested chain: a failed file open (observed through a read on
 *       the unopened handle) -> a failed JSON parse -> a closed/invalid
 *       network connection, then a successful API clears the record;
 *   P3  telemetry on/off parity for a fixed sequence of eight API failures,
 *       plus callback parity, with exact counter deltas;
 *   P4  bounded 32-cycle failure/recovery with no stale message or fields;
 *   P5  a close/drop with no `error*` argument leaves the record untouched.
 *
 * net.hp exposes close only as connection::drop(), which has no `error*`
 * parameter, so the reachable "network close" error path is a read/write on
 * an invalid (closed / borrowed fd -1) connection; this is documented in
 * error_NOTES.md and is not treated as a library defect.
 *
 * Build needs the yyjson static library:
 *   -I<root>/third_party/yyjson -L<root>/build/ext -lyyjson
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"
#include "stdlib/json.hp"
#include "stdlib/net.hp"
#include "stdlib/thread.hp"

static int ehs_checks = 0;
static int ehs_failures = 0;

static void ehs_check(const char *tag, int cond) {
    ehs_checks++;
    if (!cond)
        ehs_failures++;
    print(tag, cond ? 1 : 0);
}

/* A callback used only for the parity probe (counts, never recurses). */
static int ehs_callback_calls = 0;
static int ehs_callback_bad_cat = 0;
static void ehs_count_cb(int category, cplus_exit_code code,
                         const char *message) {
    (void)code;
    ehs_callback_calls++;
    if (category < 0 || category > STDLIB_CATEGORY_UTF8)
        ehs_callback_bad_cat++;
    if (message == (const char *)0)
        ehs_callback_bad_cat++;
}

struct ehs_sig {
    int failed;
    int category;
    int code;
    int len;
    char text[STDLIB_ERROR_SIZE];
};

static void ehs_capture(struct ehs_sig *out, error *slot) {
    int n;
    out->failed = slot->failed_now();
    out->category = slot->category();
    out->code = slot->code();
    n = (int)strlen(slot->what());
    out->len = n;
    if (n > (int)STDLIB_ERROR_SIZE - 1)
        n = (int)STDLIB_ERROR_SIZE - 1;
    __builtin_memset(out->text, 0, sizeof(out->text));
    __builtin_memcpy(out->text, slot->what(), (size_t)n);
}

static int ehs_equal(const struct ehs_sig *a, const struct ehs_sig *b) {
    int k;
    if (a->failed != b->failed || a->category != b->category ||
        a->code != b->code || a->len != b->len)
        return 0;
    for (k = 0; k < (int)STDLIB_ERROR_SIZE; k++) {
        if (a->text[k] != b->text[k])
            return 0;
    }
    return 1;
}

/* Fixed eight-failure sequence used for the on/off parity comparison. */
static void ehs_failure_sequence(error *slot, char *buf) {
    file ehs_f = file("/definitely-missing-cplus-ehs-path/file.txt", "r");
    connection ehs_c = connection_borrow(-1);
    thread ehs_t = thread();

    read_line((char *)0, 0u, slot);
    fs_size((const char *)0, slot);
    ehs_f.read((char *)0, 0u, slot);
    ehs_f.read(buf, 16u, slot);
    {
        json ehs_j = json("{", slot);
        ehs_j.drop();
    }
    {
        json ehs_pf = json_parse_file(
            "/definitely-missing-cplus-ehs-path/config.json", slot);
        ehs_pf.drop();
    }
    ehs_c.read(buf, 16u, slot);
    ehs_t.join((void **)0, slot);
}

int main(void) {
    stdlib_telemetry_stats ehs_tm_on;
    stdlib_telemetry_stats ehs_tm_off;
    struct ehs_sig ehs_sig_on[8];
    struct ehs_sig ehs_sig_off[8];
    int ehs_i;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(0);

    /* ---- P1: category/code mapping produced by each API family ----------- */
    {
        error ehs_e = error();
        char ehs_buf[16];

        read_line((char *)0, 0u, &ehs_e);
        ehs_check("ehs_p1_io_null_buf",
                  ehs_e.failed_now() == 1 &&
                      ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                      ehs_e.code() == (int)CPLUS_EXIT_IO &&
                      strcmp(ehs_e.what(),
                             "read_line: destination is null or empty") == 0);

        fs_size((const char *)0, &ehs_e);
        ehs_check("ehs_p1_fs_null_path",
                  ehs_e.failed_now() == 1 &&
                      ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                      ehs_e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                      strcmp(ehs_e.what(), "fs_size: null path") == 0);

        fs_size("/definitely-missing-cplus-ehs-path/nope", &ehs_e);
        ehs_check("ehs_p1_fs_missing",
                  ehs_e.failed_now() == 1 &&
                      ehs_e.category() == STDLIB_CATEGORY_IO &&
                      ehs_e.code() == (int)CPLUS_EXIT_IO &&
                      ehs_e.what()[0] != '\0');

        {
            file ehs_failed_open =
                file("/definitely-missing-cplus-ehs-path/file.txt", "r");
            ehs_check("ehs_p1_file_open_invalid",
                      ehs_failed_open.is_open() == 0 &&
                          ehs_failed_open.native() == (FILE *)0);
            ehs_failed_open.read(ehs_buf, sizeof(ehs_buf), &ehs_e);
            ehs_check("ehs_p1_file_read_closed",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_IO &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_e.what(), "file::read: null handle") == 0);
            ehs_failed_open.close(&ehs_e);
            ehs_check("ehs_p1_file_close_twice",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_LIFECYCLE &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_e.what(),
                                 "file::close: handle already closed") == 0);
        }

        {
            json ehs_bad = json("{", &ehs_e);
            ehs_check("ehs_p1_json_parse_bad",
                      ehs_bad.valid() == 0 && ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                          ehs_e.what()[0] != '\0');
            ehs_bad.drop();

            json ehs_missing = json_parse_file(
                "/definitely-missing-cplus-ehs-path/config.json", &ehs_e);
            ehs_check("ehs_p1_json_parse_file_missing",
                      ehs_missing.valid() == 0 && ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_IO &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          ehs_e.what()[0] != '\0');
            ehs_missing.drop();

            json ehs_null = json_parse_file((const char *)0, &ehs_e);
            ehs_check("ehs_p1_json_parse_file_null",
                      ehs_null.valid() == 0 && ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_e.what(),
                                 "json::parse_file: null path") == 0);
            ehs_null.drop();
        }

        {
            connection ehs_closed = connection_borrow(-1);
            ehs_closed.read(ehs_buf, sizeof(ehs_buf), &ehs_e);
            ehs_check("ehs_p1_net_closed_read",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_e.what(),
                                 "connection::read: invalid argument") == 0);
            ehs_closed.write("x", 1u, &ehs_e);
            ehs_check("ehs_p1_net_closed_write",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_e.what(),
                                 "connection::write: invalid argument") == 0);
            ehs_closed.drop(); /* no error* argument */
            ehs_check("ehs_p1_net_drop_no_stale",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_e.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_e.what(),
                                 "connection::write: invalid argument") == 0);
        }

        {
            thread ehs_t = thread();
            ehs_t.start((void *(*)(void *))0, (void *)0, &ehs_e);
            ehs_check("ehs_p1_thread_start_null",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_e.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                          strcmp(ehs_e.what(),
                                 "thread::start: null function") == 0);
            ehs_t.join((void **)0, &ehs_e);
            ehs_check("ehs_p1_thread_join_unjoinable",
                      ehs_e.failed_now() == 1 &&
                          ehs_e.category() == STDLIB_CATEGORY_LIFECYCLE &&
                          ehs_e.code() == (int)CPLUS_EXIT_RESOURCE &&
                          strcmp(ehs_e.what(),
                                 "thread::join: thread is not joinable") == 0);
        }
    }

    /* ---- P2: chained propagation file-open -> JSON -> network close ------ */
    {
        error ehs_chain = error();
        char ehs_buf[16];
        int ehs_k;
        char ehs_long[STDLIB_ERROR_SIZE + 16];
        for (ehs_k = 0; ehs_k < STDLIB_ERROR_SIZE + 8; ehs_k++)
            ehs_long[ehs_k] = 'F';
        ehs_long[STDLIB_ERROR_SIZE + 8] = '\0';

        /* Seed a long message from an API failure. */
        error_fail_cat(&ehs_chain, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO, ehs_long);
        ehs_check("ehs_p2_seeded_long",
                  (int)strlen(ehs_chain.what()) == (int)STDLIB_ERROR_SIZE - 1 &&
                      ehs_chain.category() == STDLIB_CATEGORY_IO);

        /* Failed file open: the unopened handle reports IO / CPLUS_EXIT_IO. */
        {
            file ehs_open = file("/definitely-missing-cplus-ehs-path/chain", "r");
            ehs_open.read(ehs_buf, sizeof(ehs_buf), &ehs_chain);
            ehs_check("ehs_p2_file_error_replaces_seed",
                      ehs_chain.failed_now() == 1 &&
                          ehs_chain.category() == STDLIB_CATEGORY_IO &&
                          ehs_chain.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_chain.what(), "file::read: null handle") ==
                              0 &&
                          ehs_chain.what()[(int)STDLIB_ERROR_SIZE - 1] == '\0');
        }

        /* Failed JSON parse: INVALID / UNSUPPORTED, no file-message tail. */
        {
            json ehs_parse = json("{ not json", &ehs_chain);
            ehs_check("ehs_p2_json_error_replaces_file",
                      ehs_parse.valid() == 0 && ehs_chain.failed_now() == 1 &&
                          ehs_chain.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_chain.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                          strcmp(ehs_chain.what(),
                                 "json::parse_file: null path") != 0 &&
                          ehs_chain.what()[0] != '\0');
            ehs_parse.drop();
        }

        /* Closed network connection: INVALID / CPLUS_EXIT_IO, no stale tail. */
        {
            connection ehs_conn = connection_borrow(-1);
            ehs_conn.read(ehs_buf, sizeof(ehs_buf), &ehs_chain);
            ehs_check("ehs_p2_net_error_replaces_json",
                      ehs_chain.failed_now() == 1 &&
                          ehs_chain.category() == STDLIB_CATEGORY_INVALID &&
                          ehs_chain.code() == (int)CPLUS_EXIT_IO &&
                          strcmp(ehs_chain.what(),
                                 "connection::read: invalid argument") == 0 &&
                          (int)strlen(ehs_chain.what()) == 34);
            ehs_conn.drop();
            ehs_check("ehs_p2_after_drop_unchanged",
                      ehs_chain.failed_now() == 1 &&
                          strcmp(ehs_chain.what(),
                                 "connection::read: invalid argument") == 0);
        }

        /* A successful API clears the whole record. */
        {
            json ehs_ok = json("{}", &ehs_chain);
            ehs_check("ehs_p2_success_clears",
                      ehs_ok.valid() == 1 && ehs_chain.ok() == 1 &&
                          ehs_chain.failed_now() == 0 &&
                          ehs_chain.category() == STDLIB_CATEGORY_OK &&
                          ehs_chain.code() == (int)CPLUS_EXIT_OK &&
                          strcmp(ehs_chain.what(), "") == 0);
            ehs_ok.drop();
        }
    }

    /* ---- P3: telemetry on/off parity for eight API failures -------------- */
    {
        error ehs_p = error();
        char ehs_pbuf[16];
        int ehs_same = 1;

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback((stdlib_error_callback)0);
        ehs_failure_sequence(&ehs_p, ehs_pbuf);
        ehs_capture(&ehs_sig_on[0], &ehs_p);
        stdlib_telemetry_snapshot(&ehs_tm_on);

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(0);
        ehs_failure_sequence(&ehs_p, ehs_pbuf);
        ehs_capture(&ehs_sig_off[0], &ehs_p);
        stdlib_telemetry_snapshot(&ehs_tm_off);

        ehs_check("ehs_p3_record_parity", ehs_equal(&ehs_sig_on[0], &ehs_sig_off[0]));
        ehs_check("ehs_p3_off_zero",
                  ehs_tm_off.api_failures == 0ULL &&
                      ehs_tm_off.invalid_ops == 0ULL &&
                      ehs_tm_off.lifecycle_failures == 0ULL &&
                      ehs_tm_off.alloc_failures == 0ULL);
        ehs_check("ehs_p3_on_exact",
                  ehs_tm_on.api_failures == 8ULL &&
                      ehs_tm_on.invalid_ops == 7ULL &&
                      ehs_tm_on.lifecycle_failures == 1ULL &&
                      ehs_tm_on.bounds_violations == 0ULL &&
                      ehs_tm_on.ownership_violations == 0ULL &&
                      ehs_tm_on.alloc_failures == 0ULL);

        /* Callback parity: enabled telemetry must not change callback count. */
        ehs_callback_calls = 0;
        ehs_callback_bad_cat = 0;
        stdlib_set_error_callback(ehs_count_cb);
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        ehs_failure_sequence(&ehs_p, ehs_pbuf);
        ehs_check("ehs_p3_callback_on",
                  ehs_callback_calls == 8 && ehs_callback_bad_cat == 0);
        ehs_callback_calls = 0;
        stdlib_telemetry_set_enabled(0);
        ehs_failure_sequence(&ehs_p, ehs_pbuf);
        ehs_check("ehs_p3_callback_off",
                  ehs_callback_calls == 8 && ehs_callback_bad_cat == 0);
        stdlib_set_error_callback((stdlib_error_callback)0);
        stdlib_telemetry_set_enabled(0);
        (void)ehs_same;
    }

    /* ---- P4: bounded 32 failure/recovery cycles, no stale state ---------- */
    {
        error ehs_cycle = error();
        char ehs_buf[16];
        int ehs_round;
        int ehs_ok = 1;
        for (ehs_round = 0; ehs_round < 32; ehs_round++) {
            if ((ehs_round % 3) == 0) {
                fs_size("/definitely-missing-cplus-ehs-path/nope", &ehs_cycle);
                if (ehs_cycle.category() != STDLIB_CATEGORY_IO ||
                    ehs_cycle.code() != (int)CPLUS_EXIT_IO)
                    ehs_ok = 0;
            } else if ((ehs_round % 3) == 1) {
                json ehs_j = json("{", &ehs_cycle);
                if (ehs_j.valid() != 0 ||
                    ehs_cycle.category() != STDLIB_CATEGORY_INVALID ||
                    ehs_cycle.code() != (int)CPLUS_EXIT_UNSUPPORTED)
                    ehs_ok = 0;
                ehs_j.drop();
            } else {
                json ehs_good = json("{\"k\":1}", &ehs_cycle);
                if (ehs_good.valid() == 0 || ehs_cycle.ok() != 1 ||
                    strcmp(ehs_cycle.what(), "") != 0)
                    ehs_ok = 0;
                ehs_good.drop();
            }
            /* Every failing iteration must end with a bounded, NUL-terminated
             * message; a success must leave the empty string. */
            if ((int)strlen(ehs_cycle.what()) >= (int)STDLIB_ERROR_SIZE)
                ehs_ok = 0;
            if (ehs_cycle.what()[(int)STDLIB_ERROR_SIZE - 1] != '\0')
                ehs_ok = 0;
            (void)ehs_buf;
        }
        ehs_check("ehs_p4_32_cycles", ehs_ok == 1);
        error_reset(&ehs_cycle);
        ehs_check("ehs_p4_final_clean",
                  ehs_cycle.ok() == 1 && ehs_cycle.code() == 0 &&
                  ehs_cycle.category() == STDLIB_CATEGORY_OK &&
                  strcmp(ehs_cycle.what(), "") == 0);
    }

    print("ERROR_HUB_SYSTEM", ehs_checks, ehs_failures);
    return ehs_failures == 0 ? 0 : 1;
}

/* Independent cross-family regression test for the modern C+ stdlib headers.
 *
 * Scope: interactions BETWEEN families, not a repeat of any single-family deep
 * suite.  Every scenario below uses only documented, recoverable contracts:
 * no fatal probes, no dereference of a stale/stale-alias pointer, no use of a
 * moved-from owner, and no view used after its owner is dropped.
 *
 *   CF1  string -> strmap key copy: source mutated (in place and through a
 *        reallocation) and finally dropped; the map keeps its own key.
 *   CF2  vector of recorded error codes across 4->8 growth and 8->64 reserve,
 *        with injected allocation failures; failure-atomic state + slice
 *        generation, then recovery.
 *   CF3  intmap moved into a second owner with cplus_move; a separately owned
 *        stdlib block populated from the map is transferred with
 *        stdlib_transfer_ownership, and a saved alias is observed as stale via
 *        its allocation generation (never dereferenced after the transfer).
 *   CF4  file::read_line into a char string buffer at the 1-byte and exact-fit
 *        boundaries; the error record carries the documented category.  Also
 *        boundaries; exact-fit now aligns with io.hp after the file.hp fix
 *        (both paths are recoverable, neither uses a stale buffer).
 *   CF5  json document/builder holding a value copied from a temporary string
 *        (via string_view) that is dropped before the document is read.
 *   CF6  a single error callback installed for every family plus telemetry
 *        counter/callback-count consistency across the resulting notes.
 *
 * Every record is a deterministic PASS/FAIL check; the last stdout line is
 * `CROSS_FAMILY <checks> <failures>` and the process exits 0 only when there
 * are no failures.  All owning locals have unique names: the C+ RAII lowering
 * tracks owners by name for a whole function (see DEEP_NET_CONTRACT.md #4).
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/file.hp"
#include "stdlib/io.hp"
#include "stdlib/json.hp"
#include "stdlib/log.hp"

static int cf_checks = 0;
static int cf_failures = 0;

static void cf_expect(int ok, const char *label) {
    cf_checks++;
    if (!ok) {
        cf_failures++;
        print("CF-FAIL", label);
    }
}

static void cf_scenario(int id, int first_check, int first_failure) {
    print("CF", id, cf_checks - first_check, cf_failures - first_failure);
}

/* ---- CF6 callback state ------------------------------------------------- */

static int cf6_calls = 0;
static int cf6_phase = -1;
static int cf6_phase_calls[6];
static unsigned long long cf6_categories = 0ULL;
static int cf6_last_category = -1;
static int cf6_last_code = -1;

static void cf6_callback(int category, cplus_exit_code code,
                         const char *message) {
    (void)message;
    cf6_calls++;
    if (cf6_phase >= 0 && cf6_phase < 6)
        cf6_phase_calls[cf6_phase]++;
    cf6_categories |= (1ULL << (unsigned int)category);
    cf6_last_category = category;
    cf6_last_code = (int)code;
}

int main(void) {
    stdlib_telemetry_stats cf_tm;
    stdlib_telemetry_stats cf_tm_pre;
    int cf_first_check;
    int cf_first_failure;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(0);

    /* ---- CF1: string key copied into strmap ----------------------------- */
    cf_first_check = cf_checks;
    cf_first_failure = cf_failures;
    {
        string cf1_source = string("alpha");
        strmap cf1_map = strmap();
        int cf1_value = -1;
        const char *cf1_old_buffer = cf1_source.c_str();
        unsigned long long cf1_old_gen = stdlib_alloc_generation(cf1_old_buffer);

        cf_expect(cf1_source.valid() == 1 && cf1_old_gen != 0ULL &&
                      cf1_map.valid() == 0,
                  "cf1_init");
        cf_expect(cf1_map.put(cf1_source.c_str(), 7) == 0 && cf1_map.size() == 1,
                  "cf1_put");
        cf_expect(cf1_map.get("alpha", &cf1_value) == 0 && cf1_value == 7,
                  "cf1_get_before");

        /* in-place mutation: the key must not follow the source buffer */
        cf1_source.clear();
        cf_expect(cf1_source.size() == 0 && cf1_old_buffer[0] == '\0',
                  "cf1_clear_source");
        cf_expect(cf1_map.get("alpha", &cf1_value) == 0 && cf1_value == 7,
                  "cf1_key_after_clear");

        /* reallocating mutation: old buffer is released, map key survives */
        cf_expect(cf1_source.reserve(64) == 0 && cf1_source.append("beta") == 0 &&
                      cf1_source.equals("beta") == 1,
                  "cf1_realloc_source");
        cf_expect(cf1_map.get("alpha", &cf1_value) == 0 && cf1_value == 7,
                  "cf1_key_after_realloc");
        cf_expect(cf1_map.get("beta", &cf1_value) == -1,
                  "cf1_no_beta_key");
        cf_expect(stdlib_alloc_check_generation(cf1_old_buffer, cf1_old_gen) == 0,
                  "cf1_old_buffer_stale");

        /* source destroyed entirely: the map still owns its key copy */
        cf1_source.drop();
        cf_expect(cf1_source.valid() == 0 && cf1_source.c_str()[0] == '\0',
                  "cf1_source_dropped");
        cf_expect(cf1_map.get("alpha", &cf1_value) == 0 && cf1_value == 7,
                  "cf1_key_after_drop");
        cf_expect(cf1_map.remove("alpha") == 0 &&
                      cf1_map.get("alpha", &cf1_value) == -1 &&
                      cf1_map.size() == 0,
                  "cf1_remove");
        cf1_map.drop();
        cf_expect(cf1_map.valid() == 0, "cf1_map_dropped");
    }
    cf_scenario(1, cf_first_check, cf_first_failure);

    /* ---- CF2: vector of error codes, growth and injected OOM ------------ */
    cf_first_check = cf_checks;
    cf_first_failure = cf_failures;
    {
        vector cf2_errors = vector();
        error cf2_record = error();
        slice cf2_snapshot;
        int cf2_expected[9];
        int cf2_out = -1;
        int cf2_index = 0;
        int cf2_size = 0;
        int cf2_capacity = 0;
        int cf2_ok = 1;

        cf2_expected[0] = (int)CPLUS_EXIT_IO;
        cf2_expected[1] = (int)CPLUS_EXIT_RESOURCE;
        cf2_expected[2] = (int)CPLUS_EXIT_BOUNDS;
        cf2_expected[3] = (int)CPLUS_EXIT_OWNERSHIP;
        cf2_expected[4] = (int)CPLUS_EXIT_UNSUPPORTED;
        cf2_expected[5] = (int)CPLUS_EXIT_IO;
        cf2_expected[6] = 77;
        cf2_expected[7] = 88;

        /* Reset once, before any allocation, so the final balance check sees
         * every allocation and free performed in this scenario. */
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(NULL);

        for (cf2_index = 0; cf2_index < 8; cf2_index++) {
            if (cf2_index < 6)
                cf2_record.set((cplus_exit_code)cf2_expected[cf2_index], "cf2");
            if (cf2_errors.push_back(cf2_expected[cf2_index]) != 0)
                cf2_ok = 0;
        }
        cf_expect(cf2_ok == 1 && cf2_errors.size() == 8 &&
                      cf2_errors.capacity() == 8,
                  "cf2_growth_4_to_8");
        for (cf2_index = 0; cf2_index < 8; cf2_index++) {
            if (cf2_errors.at_checked((size_t)cf2_index, &cf2_out) != 0 ||
                cf2_out != cf2_expected[cf2_index])
                cf2_ok = 0;
        }
        cf_expect(cf2_ok == 1, "cf2_codes_preserved");

        cf2_snapshot = cf2_errors.view();
        cf_expect(cf2_snapshot.valid() == 1 && cf2_snapshot.stale() == 0 &&
                      cf2_snapshot.size() == 8,
                  "cf2_slice_fresh");

        /* size == capacity: every allocation fails, growth rolls back */
        stdlib_telemetry_snapshot(&cf_tm_pre);
        stdlib_alloc_fail_after(0);
        cf2_size = (int)cf2_errors.size();
        cf2_capacity = (int)cf2_errors.capacity();
        cf_expect(cf2_errors.push_back(999) == -1, "cf2_push_oom");
        cf_expect((int)cf2_errors.size() == cf2_size &&
                      (int)cf2_errors.capacity() == cf2_capacity,
                  "cf2_push_oom_atomic");
        cf_expect(cf2_snapshot.valid() == 1 && cf2_snapshot.stale() == 0,
                  "cf2_slice_after_failed_push");
        stdlib_telemetry_snapshot(&cf_tm);
        cf_expect(cf_tm.alloc_failures - cf_tm_pre.alloc_failures == 1 &&
                      cf_tm.api_failures > cf_tm_pre.api_failures,
                  "cf2_oom_telemetry");
        stdlib_alloc_fail_reset();

        cf_expect(cf2_errors.push_back(99) == 0, "cf2_push_recovered");
        cf2_expected[8] = 99;
        cf_expect(cf2_errors.capacity() == 16 && cf2_errors.size() == 9 &&
                      cf2_snapshot.stale() == 1 && cf2_snapshot.valid() == 0 &&
                      cf2_snapshot.at_checked(0, &cf2_out) == -1,
                  "cf2_slice_stale_after_growth");

        /* explicit reserve OOM is atomic too, then succeeds after reset */
        stdlib_telemetry_snapshot(&cf_tm_pre);
        stdlib_alloc_fail_after(0);
        cf_expect(cf2_errors.reserve(64) == -1, "cf2_reserve_oom");
        cf_expect(cf2_errors.size() == 9 && cf2_errors.capacity() == 16,
                  "cf2_reserve_oom_atomic");
        stdlib_telemetry_snapshot(&cf_tm);
        cf_expect(cf_tm.alloc_failures - cf_tm_pre.alloc_failures == 1,
                  "cf2_reserve_oom_telemetry");
        stdlib_alloc_fail_reset();
        cf_expect(cf2_errors.reserve(64) == 0 && cf2_errors.capacity() == 64,
                  "cf2_reserve_recovered");

        cf2_ok = 1;
        for (cf2_index = 0; cf2_index < 9; cf2_index++) {
            if (cf2_errors.at_checked((size_t)cf2_index, &cf2_out) != 0 ||
                cf2_out != cf2_expected[cf2_index])
                cf2_ok = 0;
        }
        cf_expect(cf2_ok == 1, "cf2_codes_after_recovery");

        cf2_errors.drop();
        stdlib_telemetry_snapshot(&cf_tm);
        cf_expect(cf_tm.alloc_count > 0 && cf_tm.free_count == cf_tm.alloc_count,
                  "cf2_alloc_balance");
        stdlib_telemetry_set_enabled(0);
    }
    cf_scenario(2, cf_first_check, cf_first_failure);

    /* ---- CF3: moved intmap + generation-observed stale alias ------------ */
    cf_first_check = cf_checks;
    cf_first_failure = cf_failures;
    {
        intmap cf3_origin = intmap();
        intmap cf3_owner;
        void *cf3_payload = NULL;
        void *cf3_alias = NULL;
        void *cf3_receiver = NULL;
        int *cf3_payload_view = NULL;
        unsigned long long cf3_generation = 0ULL;
        int cf3_index = 0;
        int cf3_value = -1;
        int cf3_ok = 1;

        for (cf3_index = 0; cf3_index < 8; cf3_index++)
            cf3_origin.put(cf3_index + 1, (cf3_index + 1) * 11);
        cf_expect(cf3_origin.size() == 8, "cf3_origin_filled");

        cf3_payload = stdlib_calloc(8, sizeof(int));
        cf_expect(cf3_payload != NULL, "cf3_payload_alloc");
        cf3_payload_view = (int *)cf3_payload;
        for (cf3_index = 0; cf3_index < 8; cf3_index++) {
            cf3_origin.get(cf3_index + 1, &cf3_value);
            cf3_payload_view[cf3_index] = cf3_value;
        }
        cf3_generation = stdlib_alloc_generation(cf3_payload);
        cf3_alias = cf3_payload; /* saved before ownership changes */
        cf_expect(cf3_generation != 0ULL &&
                      stdlib_alloc_check_generation(cf3_alias, cf3_generation) == 1,
                  "cf3_generation_before");

        /* The collection itself moves; the moved-from owner is never read. */
        cf3_owner = cplus_move(cf3_origin);
        cf_expect(cf3_owner.size() == 8 &&
                      cf3_owner.get(3, &cf3_value) == 0 && cf3_value == 33,
                  "cf3_intmap_moved");

        cf_expect(stdlib_transfer_ownership(&cf3_receiver, &cf3_payload) == 0,
                  "cf3_transfer");
        cf_expect(cf3_payload == NULL && cf3_receiver == cf3_alias,
                  "cf3_transfer_handles");
        cf_expect(stdlib_alloc_check_generation(cf3_receiver, cf3_generation) == 0 &&
                      stdlib_alloc_check_generation(cf3_alias, cf3_generation) == 0,
                  "cf3_stale_alias_generation");
        cf_expect(stdlib_alloc_generation(cf3_receiver) != 0ULL &&
                      stdlib_alloc_generation(cf3_receiver) != cf3_generation,
                  "cf3_new_generation");
        cf_expect(stdlib_alloc_length(cf3_receiver) == 8, "cf3_moved_length");

        /* Read through the new owner only; cf3_alias is never dereferenced. */
        cf3_payload_view = (int *)cf3_receiver;
        for (cf3_index = 0; cf3_index < 8; cf3_index++) {
            cf3_owner.get(cf3_index + 1, &cf3_value);
            if (cf3_payload_view[cf3_index] != cf3_value)
                cf3_ok = 0;
        }
        cf_expect(cf3_ok == 1, "cf3_payload_matches_moved_map");
        cf_expect(stdlib_transfer_ownership(&cf3_receiver, &cf3_payload) == -1,
                  "cf3_second_transfer_rejected");

        stdlib_release(cf3_receiver);
        cf_expect(stdlib_alloc_generation(cf3_receiver) == 0ULL,
                  "cf3_released_generation");
        cf3_owner.drop();
        cf_expect(cf3_owner.valid() == 0, "cf3_owner_dropped");
    }
    cf_scenario(3, cf_first_check, cf_first_failure);

    /* ---- CF4: file read_line boundaries and error categories ------------ */
    cf_first_check = cf_checks;
    cf_first_failure = cf_failures;
    {
        error cf4_err = error();
        file cf4_stream = file_temp(&cf4_err);
        char cf4_buffer[16];
        int cf4_status = -2;
        int cf4_file_eof_status = -2;

        cf_expect(cf4_stream.is_open() == 1 && cf4_err.ok() == 1, "cf4_open");
        cf4_stream.write("abc\nx\n\nABCDEFGH\nOK\nlast");
        cf_expect(cf4_stream.flush(&cf4_err) == STDLIB_OK, "cf4_flush");
        cf_expect(cf4_stream.seek(0, SEEK_SET, &cf4_err) == STDLIB_OK,
                  "cf4_seek");

        /* exact fit including the newline: "abc\n" in a 5-byte buffer */
        cf4_status = cf4_stream.read_line(cf4_buffer, 5, &cf4_err);
        cf_expect(cf4_status == STDLIB_OK && strcmp(cf4_buffer, "abc") == 0 &&
                      cf4_err.ok() == 1 && cf4_err.category() == STDLIB_CATEGORY_OK,
                  "cf4_exact_fit_newline");

        /* 1-byte buffer with a longer line: drained, FAIL / BOUNDS */
        cf4_status = cf4_stream.read_line(cf4_buffer, 1, &cf4_err);
        cf_expect(cf4_status == STDLIB_FAIL && cf4_buffer[0] == '\0' &&
                      cf4_err.failed_now() == 1 &&
                      cf4_err.category() == STDLIB_CATEGORY_BOUNDS &&
                      cf4_err.code() == (int)CPLUS_EXIT_BOUNDS,
                  "cf4_one_byte_bounds");

        /* 1-byte buffer with an empty line: OK, still empty */
        cf4_status = cf4_stream.read_line(cf4_buffer, 1, &cf4_err);
        cf_expect(cf4_status == STDLIB_OK && cf4_buffer[0] == '\0' &&
                      cf4_err.ok() == 1,
                  "cf4_one_byte_empty_line");

        /* overlong for size 8: FAIL / BOUNDS, buffer emptied, stream aligned */
        cf4_status = cf4_stream.read_line(cf4_buffer, 8, &cf4_err);
        cf_expect(cf4_status == STDLIB_FAIL && cf4_buffer[0] == '\0' &&
                      cf4_err.category() == STDLIB_CATEGORY_BOUNDS,
                  "cf4_overlong_bounds");
        cf4_status = cf4_stream.read_line(cf4_buffer, 8, &cf4_err);
        cf_expect(cf4_status == STDLIB_OK && strcmp(cf4_buffer, "OK") == 0 &&
                      cf4_err.ok() == 1,
                  "cf4_stream_stays_aligned");

        /* REGRESSION (was a real defect): a line whose content is exactly
         * size-1 bytes at physical EOF FITS the buffer.  io.hp::read_line
         * peeks the next byte and accepts it; file.hp used to rely on feof(),
         * which libc has not set after fgets() consumed exactly size-1 bytes,
         * and wrongly reported FAIL / BOUNDS.  file.hp now uses the same peek
         * logic, so both readers must agree. */
        cf4_file_eof_status = cf4_stream.read_line(cf4_buffer, 5, &cf4_err);
        cf_expect(cf4_file_eof_status == STDLIB_OK &&
                      strcmp(cf4_buffer, "last") == 0 && cf4_err.ok() == 1,
                  "cf4_file_full_buffer_eof_aligned");
        cf4_status = cf4_stream.read_line(cf4_buffer, 5, &cf4_err);
        cf_expect(cf4_status == STDLIB_EOF && cf4_err.ok() == 1, "cf4_eof");
        cf4_stream.drop();

        /* Cross-family boundary: content length == size-1 followed by '\n'.
         * After the exact-fit fix both readers peek the delimiter and accept
         * it; neither reads a stale buffer. */
        {
            FILE *cf4_saved_stdin = stdin;
            FILE *cf4_io_source = tmpfile();
            file cf4_file_source = file_temp(&cf4_err);
            int cf4_io_status = -2;
            int cf4_file_status = -2;
            char cf4_io_buffer[8];
            char cf4_file_buffer[8];

            cf_expect(cf4_io_source != NULL && cf4_file_source.is_open() == 1,
                      "cf4_sources_open");
            fputs("abcd\nZ\n", cf4_io_source);
            rewind(cf4_io_source);
            cf4_file_source.write("abcd\nZ\n");
            cf4_file_source.flush(&cf4_err);
            cf4_file_source.seek(0, SEEK_SET, &cf4_err);

            stdin = cf4_io_source;
            cf4_io_status = read_line(cf4_io_buffer, 5, &cf4_err);
            stdin = cf4_saved_stdin;
            cf_expect(cf4_io_status == STDLIB_OK &&
                          strcmp(cf4_io_buffer, "abcd") == 0 && cf4_err.ok() == 1,
                      "cf4_io_exact_fit");

            cf4_file_status = cf4_file_source.read_line(cf4_file_buffer, 5,
                                                        &cf4_err);
            cf_expect(cf4_file_status == STDLIB_OK &&
                          strcmp(cf4_file_buffer, "abcd") == 0 &&
                          cf4_err.ok() == 1,
                      "cf4_file_exact_fit_aligned");
            cf_expect(cf4_io_status == cf4_file_status,
                      "cf4_cross_family_aligned");

            /* Second exact-fit boundary at physical EOF: both readers now
             * peek fgetc(), see EOF and accept the full line. */
            {
                FILE *cf4_io_eof_source = tmpfile();
                char cf4_io_eof_buffer[8];
                int cf4_io_eof_status = -2;
                cf_expect(cf4_io_eof_source != NULL, "cf4_io_eof_source_open");
                fputs("last", cf4_io_eof_source);
                rewind(cf4_io_eof_source);
                stdin = cf4_io_eof_source;
                cf4_io_eof_status = read_line(cf4_io_eof_buffer, 5, &cf4_err);
                stdin = cf4_saved_stdin;
                cf_expect(cf4_io_eof_status == STDLIB_OK &&
                              strcmp(cf4_io_eof_buffer, "last") == 0 &&
                              cf4_err.ok() == 1,
                          "cf4_io_full_buffer_eof");
                cf_expect(cf4_io_eof_status == cf4_file_eof_status,
                          "cf4_eof_aligned");
                fclose(cf4_io_eof_source);
            }

            /* both streams are still aligned to the following line */
            cf4_file_status = cf4_file_source.read_line(cf4_file_buffer, 8,
                                                        &cf4_err);
            cf_expect(cf4_file_status == STDLIB_OK &&
                          strcmp(cf4_file_buffer, "Z") == 0,
                      "cf4_file_drained_aligned");
            cf4_file_source.drop();
            fclose(cf4_io_source);
        }
    }
    cf_scenario(4, cf_first_check, cf_first_failure);

    /* ---- CF5: JSON value copied from a temporary string ----------------- */
    cf_first_check = cf_checks;
    cf_first_failure = cf_failures;
    {
        error cf5_err = error();
        string cf5_temporary = string("persist-me");
        string_view cf5_borrowed = cf5_temporary.to_view();
        json_builder cf5_builder = json_builder();
        char *cf5_text = NULL;
        size_t cf5_length = 0;
        const char *cf5_value = NULL;
        unsigned int cf5_bit = 0;

        cf_expect(cf5_borrowed.size() == 10 &&
                      cf5_borrowed.equals("persist-me") == 1,
                  "cf5_view_init");
        cf_expect(cf5_builder.valid() == 1, "cf5_builder_valid");
        cf_expect(cf5_builder.add_string("k", cf5_borrowed.data(), &cf5_err) ==
                      STDLIB_OK && cf5_err.ok() == 1,
                  "cf5_builder_copies_string");

        /* the borrowed string is gone; the builder owns a copy of the value */
        cf5_temporary.drop();
        cf5_text = cf5_builder.write(&cf5_length);
        cf_expect(cf5_text != NULL && cf5_length > 0 &&
                      strstr(cf5_text, "persist-me") != NULL,
                  "cf5_builder_survives_source_drop");

        {
            json cf5_document = json(cf5_text, &cf5_err);
            json_view cf5_root = cf5_document.root();
            json_view cf5_view = cf5_root.member("k");
            cf_expect(cf5_document.valid() == 1 && cf5_err.ok() == 1,
                      "cf5_document_valid");
            cf_expect(cf5_view.valid() == 1 && cf5_view.is_string() == 1,
                      "cf5_view_valid");
            cf_expect(cf5_view.get_string(&cf5_value, &cf5_err) == STDLIB_OK &&
                          cf5_value != NULL &&
                          strcmp(cf5_value, "persist-me") == 0,
                      "cf5_view_value");
            cf5_document.drop();
            cf_expect(cf5_document.valid() == 0, "cf5_document_dropped");
            /* cf5_view is deliberately not used after the drop */
        }
        json_free(cf5_text);
        cf5_builder.drop();

        /* Parse straight from a temporary string, then drop that string:
         * yyjson READ_NOFLAG copies the input into the document. */
        {
            string cf5_source_text = string("{\"k\":\"after-drop\"}");
            json cf5_document_two = json(cf5_source_text.c_str(), &cf5_err);
            json_view cf5_view_two;
            cf5_source_text.drop();
            cf_expect(cf5_document_two.valid() == 1 && cf5_err.ok() == 1,
                      "cf5_direct_parse");
            json_view cf5_root_two = cf5_document_two.root();
            cf5_view_two = cf5_root_two.member("k");
            cf_expect(cf5_view_two.get_string(&cf5_value, &cf5_err) == STDLIB_OK &&
                          cf5_value != NULL &&
                          strcmp(cf5_value, "after-drop") == 0,
                      "cf5_view_after_source_drop");
            cf5_document_two.drop();
        }
        cf5_bit = (unsigned int)cf5_err.ok();
        cf_expect(cf5_bit == 1u, "cf5_error_clean");
    }
    cf_scenario(5, cf_first_check, cf_first_failure);

    /* ---- CF6: one callback for every family + telemetry consistency ----- */
    cf_first_check = cf_checks;
    cf_first_failure = cf_failures;
    {
        string cf6_string = string("x");
        vector cf6_vector = vector();
        strmap cf6_map = strmap();
        file cf6_badfile = file(NULL, NULL);
        error cf6_err = error();
        json cf6_document = json("{\"s\":\"v\"}", &cf6_err);
        json_view cf6_root = cf6_document.root();
        json_view cf6_view = cf6_root.member("s");
        log_sink cf6_sink = log_sink(tmpfile(), 0, LOG_INFO);
        log_sink cf6_dead = log_sink(NULL, 0, LOG_INFO);
        FILE *cf6_log_native = cf6_sink.native();
        void *cf6_block = NULL;
        void *cf6_from = NULL;
        void *cf6_to = NULL;
        int cf6_out = -1;
        int cf6_family = 0;
        int cf6_ok = 1;

        for (cf6_family = 0; cf6_family < 6; cf6_family++)
            cf6_phase_calls[cf6_family] = 0;
        cf6_calls = 0;
        cf6_categories = 0ULL;
        cf6_last_category = -1;
        cf6_last_code = -1;
        cf6_phase = -1;

        stdlib_alloc_fail_reset();
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(cf6_callback);

        /* a successful log write must not fire the error callback */
        cf_expect(cf6_sink.info("cf6-ok", &cf6_err) == STDLIB_OK &&
                      cf6_err.ok() == 1 && cf6_calls == 0,
                  "cf6_log_success_quiet");
        stdlib_telemetry_reset();

        /* phase 0: string */
        cf6_phase = 0;
        cf_expect(cf6_string.append((const char *)0) == -1, "cf6_string_failure");
        cf_expect(cf6_phase_calls[0] >= 1 &&
                      cf6_last_category == STDLIB_CATEGORY_INVALID &&
                      cf6_last_code == (int)CPLUS_EXIT_UNSUPPORTED,
                  "cf6_string_callback");

        /* phase 1: vector */
        cf6_phase = 1;
        cf_expect(cf6_vector.at_checked(0, &cf6_out) == -1, "cf6_vector_failure");
        cf_expect(cf6_phase_calls[1] >= 1 &&
                      cf6_last_category == STDLIB_CATEGORY_BOUNDS &&
                      cf6_last_code == (int)CPLUS_EXIT_BOUNDS,
                  "cf6_vector_callback");

        /* phase 2: collections */
        cf6_phase = 2;
        cf_expect(cf6_map.put((const char *)0, 1) == -1, "cf6_map_failure");
        cf_expect(cf6_phase_calls[2] >= 1 &&
                      cf6_last_category == STDLIB_CATEGORY_INVALID,
                  "cf6_map_callback");

        /* phase 3: file */
        cf6_phase = 3;
        cf_expect(cf6_badfile.read_line((char *)0, 0, &cf6_err) == STDLIB_FAIL &&
                      cf6_err.category() == STDLIB_CATEGORY_INVALID,
                  "cf6_file_failure");
        cf_expect(cf6_phase_calls[3] >= 1, "cf6_file_callback");

        /* phase 4: json */
        cf6_phase = 4;
        cf_expect(cf6_view.get_int(&cf6_out, &cf6_err) == STDLIB_FAIL &&
                      cf6_err.category() == STDLIB_CATEGORY_INVALID,
                  "cf6_json_failure");
        cf_expect(cf6_phase_calls[4] >= 1, "cf6_json_callback");

        /* phase 5: log */
        cf6_phase = 5;
        cf_expect(cf6_dead.info("x", &cf6_err) == STDLIB_FAIL &&
                      cf6_err.category() == STDLIB_CATEGORY_INVALID,
                  "cf6_log_failure");
        cf_expect(cf6_phase_calls[5] >= 1, "cf6_log_callback");

        /* allocator, ownership and error-family notes are not tied to a phase */
        cf6_phase = -1;
        stdlib_alloc_fail_after(0);
        cf6_block = stdlib_alloc(8);
        stdlib_alloc_fail_reset();
        cf_expect(cf6_block == NULL, "cf6_alloc_failure");
        cf_expect(stdlib_transfer_ownership(&cf6_to, &cf6_from) == -1,
                  "cf6_ownership_failure");
        error_fail_cat(&cf6_err, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO, "cf6-io");

        stdlib_telemetry_snapshot(&cf_tm);
        cf_expect(cf_tm.api_failures == 9 && cf6_calls == 9,
                  "cf6_api_failures_match_callback");
        cf_expect(cf_tm.invalid_ops == 6 && cf_tm.bounds_violations == 1 &&
                      cf_tm.alloc_failures == 1 &&
                      cf_tm.ownership_violations == 1 &&
                      cf_tm.lifecycle_failures == 0,
                  "cf6_category_buckets");
        cf6_ok = 1;
        for (cf6_family = 0; cf6_family < 6; cf6_family++)
            if (cf6_phase_calls[cf6_family] < 1)
                cf6_ok = 0;
        cf_expect(cf6_ok == 1, "cf6_all_families_reported");
        cf_expect((cf6_categories & (1ULL << STDLIB_CATEGORY_INVALID)) != 0ULL &&
                      (cf6_categories & (1ULL << STDLIB_CATEGORY_BOUNDS)) != 0ULL &&
                      (cf6_categories & (1ULL << STDLIB_CATEGORY_MEMORY)) != 0ULL &&
                      (cf6_categories & (1ULL << STDLIB_CATEGORY_OWNERSHIP)) != 0ULL &&
                      (cf6_categories & (1ULL << STDLIB_CATEGORY_IO)) != 0ULL,
                  "cf6_categories_seen");

        stdlib_set_error_callback(NULL);
        stdlib_telemetry_set_enabled(0);
        cf6_string.drop();
        cf6_map.drop();
        cf6_document.drop();
        cf6_sink.drop();
        cf6_dead.drop();
        if (cf6_log_native != NULL)
            fclose(cf6_log_native);
    }
    cf_scenario(6, cf_first_check, cf_first_failure);

    print("CROSS_FAMILY", cf_checks, cf_failures);
    return cf_failures == 0 ? 0 : 1;
}

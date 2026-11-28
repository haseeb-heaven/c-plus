/* Hostile-input corpus: stdlib/string.hp (Task 6 Phase 2).
 *
 * Self-checking: each case prints "<tag> <0|1>" (1 = expected recoverable
 * behaviour observed). The final line is "HOSTILE_STRING <checks> <failures>".
 * Exit code is 0 only when failures == 0. No check performs out-of-bounds
 * access; every probe is a documented API call with a recoverable result or a
 * metadata query. Pointer-parameter element reads use __builtin_memcpy.
 *
 * Build: ./build/c+ build <this file> --strict --sanitize=none -o <bin>
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
#include "stdlib/string.hp"

static int hs_checks = 0;
static int hs_failures = 0;

static void hs_check(const char *tag, int cond) {
    hs_checks++;
    if (!cond)
        hs_failures++;
    print(tag, cond ? 1 : 0);
}

static char hs_base_path[512];
static char hs_fixture_path[640];

/* Strict-safe byte read from a pointer parameter. */
static unsigned char hs_byte(const char *p, size_t idx) {
    unsigned char b;
    __builtin_memcpy(&b, p + idx, 1);
    return b;
}

/* Strict-safe pointer read from the argv parameter (no raw subscript). */
static char *hs_argv_ptr(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

/* Load a fixture into a caller buffer as raw bytes; returns byte count or -1. */
static long hs_load(const char *name, char *dst, size_t cap) {
    error load_err = error();
    int path_len;
    long before_pos;
    long after_pos;
    size_t total_bytes = 0;
    int read_rc;
    path_len = snprintf(hs_fixture_path, sizeof(hs_fixture_path), "%s/%s",
                        hs_base_path, name);
    if (path_len <= 0 || (size_t)path_len >= sizeof(hs_fixture_path))
        return -1;
    {
        file load_handle = file(hs_fixture_path, "rb");
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
    /* Single decimal literal: the C+ bounds checker reads the first token of
     * an array bound, so '1024 * 1024 + 2' would be mis-recorded as 1024. */
    char huge_text[1048578];
    char enc_bytes[64];

    if (argc > 1) {
        int base_len = snprintf(hs_base_path, sizeof(hs_base_path), "%s",
                                hs_argv_ptr(argv, 1));
        if (base_len <= 0 || (size_t)base_len >= sizeof(hs_base_path))
            return 2;
    } else {
        int base_len = snprintf(hs_base_path, sizeof(hs_base_path), "%s",
                                "tests/security/hostile_inputs/fixtures");
        if (base_len <= 0 || (size_t)base_len >= sizeof(hs_base_path))
            return 2;
    }

    /* ---- NULL / invalid length ----------------------------------------- */
    {
        string null_string = string(NULL);
        int null_len = hs_byte(null_string.c_str(), 0);
        hs_check("str_null_ctor_empty", null_string.valid() == 1 &&
                 null_string.size() == 0 && null_len == 0);
        hs_check("str_equals_null", null_string.equals(NULL) == 0);
        hs_check("str_compare_null", null_string.compare(NULL) == 1);
        hs_check("str_find_null", null_string.find(NULL) == -1);
        hs_check("str_append_null", null_string.append(NULL) == -1);
        {
            error copy_err = error();
            (void)copy_err;
            hs_check("str_copy_from_null", null_string.copy_from(NULL) == -1);
        }
        {
            char out_buf[8];
            hs_check("str_substr_null_out",
                     null_string.substr(0, 1, NULL, sizeof(out_buf)) == -1);
            hs_check("str_substr_zero_out_size",
                     null_string.substr(0, 1, out_buf, 0) == -1);
            hs_check("str_field_null_out",
                     null_string.field(',', 0, NULL, sizeof(out_buf)) == -1);
            hs_check("str_field_zero_out_size",
                     null_string.field(',', 0, out_buf, 0) == -1);
            hs_check("str_substr_start_oob",
                     null_string.substr(999, 1, out_buf, sizeof(out_buf)) == -1);
        }
        null_string.drop();
    }

    /* ---- string_view metadata on NULL / huge counts -------------------- */
    {
        string_view null_view = string_view(NULL, 0);
        string_view huge_view = string_view(NULL, SIZE_MAX);
        hs_check("strview_null_zero_utf8", null_view.size() == 0 &&
                 null_view.is_valid_utf8() == 1);
        hs_check("strview_null_nonzero_utf8", huge_view.is_valid_utf8() == 0);
        hs_check("strview_null_equals", null_view.equals(NULL) == 0);
        hs_check("strview_null_data", null_view.data() == NULL);
    }

    /* ---- strict UTF-8 validation (inline byte arrays) ------------------ */
    {
        char ascii_bytes[] = {'a', 'b', 'c'};
        char nul_bytes[] = {'a', '\0', 'b'};
        char overlong_bytes[] = {(char)0xC0, (char)0xAF};
        char surrogate_bytes[] = {(char)0xED, (char)0xA0, (char)0x80};
        char above_max_bytes[] = {(char)0xF4, (char)0x90, (char)0x80, (char)0x80};
        char stray_bytes[] = {(char)0x80};
        char truncated_bytes[] = {(char)0xE2, (char)0x82};
        char valid4_bytes[] = {(char)0xF0, (char)0x9F, (char)0x98, (char)0x80};
        hs_check("utf8_ascii", stdlib_utf8_validate(ascii_bytes, 3) == 1);
        hs_check("utf8_embedded_nul_valid", stdlib_utf8_validate(nul_bytes, 3) == 1);
        hs_check("utf8_overlong_rejected", stdlib_utf8_validate(overlong_bytes, 2) == 0);
        hs_check("utf8_surrogate_rejected", stdlib_utf8_validate(surrogate_bytes, 3) == 0);
        hs_check("utf8_above_max_rejected", stdlib_utf8_validate(above_max_bytes, 4) == 0);
        hs_check("utf8_stray_continuation_rejected", stdlib_utf8_validate(stray_bytes, 1) == 0);
        hs_check("utf8_truncated_rejected", stdlib_utf8_validate(truncated_bytes, 2) == 0);
        hs_check("utf8_valid_four_byte", stdlib_utf8_validate(valid4_bytes, 4) == 1);
    }

    /* ---- invalid encoding fixture + oversized-but-bounded input -------- */
    {
        long enc_count = hs_load("string_invalid_utf8.bin", enc_bytes,
                                 sizeof(enc_bytes));
        hs_check("utf8_fixture_rejected", enc_count == 12 &&
                 stdlib_utf8_validate(enc_bytes, (size_t)enc_count) == 0);
        {
            long huge_count = hs_load("json_large_1mib.json", huge_text,
                                      sizeof(huge_text));
            hs_check("utf8_large_bounded", huge_count == 1048575 &&
                     stdlib_utf8_validate(huge_text, (size_t)huge_count) == 1);
        }
    }

    /* ---- repeated delimiters ------------------------------------------- */
    {
        char delim_bytes[32];
        long delim_count = hs_load("string_delims.txt", delim_bytes,
                                   sizeof(delim_bytes));
        string delim_string = string(delim_bytes);
        char field_out[16];
        int field_rc;
        hs_check("str_delims_loaded", delim_count == 8 &&
                 delim_string.size() == 8);
        hs_check("str_delims_count", delim_string.count_fields(',') == 6);
        field_rc = delim_string.field(',', 0, field_out, sizeof(field_out));
        hs_check("str_delims_field0", field_rc == 0 && field_out[0] == 'a' &&
                 field_out[1] == '\0');
        field_rc = delim_string.field(',', 1, field_out, sizeof(field_out));
        hs_check("str_delims_empty_field", field_rc == 0 && field_out[0] == '\0');
        field_rc = delim_string.field(',', 5, field_out, sizeof(field_out));
        hs_check("str_delims_last_field", field_rc == 0 && field_out[0] == 'c' &&
                 field_out[1] == '\0');
        field_rc = delim_string.field(',', 6, field_out, sizeof(field_out));
        hs_check("str_delims_oob_field", field_rc == -1);
        delim_string.drop();
    }

    /* ---- integer-overflow guards --------------------------------------- */
    {
        string small_string = string("ok");
        hs_check("str_reserve_overflow", small_string.reserve(SIZE_MAX) == -1);
        hs_check("str_at_oob", small_string.at(9999) == '\0');
        hs_check("str_find_missing", small_string.find("zzz") == -1);
        small_string.drop();
    }

    print("HOSTILE_STRING", hs_checks, hs_failures);
    return hs_failures == 0 ? 0 : 1;
}

/* Hostile-input corpus: stdlib/io.hp and stdlib/file.hp (Task 6 Phase 2).
 *
 * Self-checking: each case prints "<tag> <0|1>"; final line is
 * "HOSTILE_IO_FILE <checks> <failures>". Exit 0 only when failures == 0.
 * stdin-facing readers are fed through freopen() from small fixtures; file.hp
 * probes use RAII handles and metadata queries only.
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
#include "stdlib/io.hp"

static int hf_checks = 0;
static int hf_failures = 0;

static void hf_check(const char *tag, int cond) {
    hf_checks++;
    if (!cond)
        hf_failures++;
    print(tag, cond ? 1 : 0);
}

static char hf_base_path[512];
static char hf_fixture_path[640];

static char *hf_argv_ptr(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static unsigned char hf_byte(const char *p, size_t idx) {
    unsigned char b;
    __builtin_memcpy(&b, p + idx, 1);
    return b;
}

/* Build <base>/<name> and redirect stdin to it. */
static int hf_switch_stdin(const char *name) {
    int path_len = snprintf(hf_fixture_path, sizeof(hf_fixture_path), "%s/%s",
                            hf_base_path, name);
    FILE *swapped;
    if (path_len <= 0 || (size_t)path_len >= sizeof(hf_fixture_path))
        return 0;
    swapped = freopen(hf_fixture_path, "r", stdin);
    return swapped != NULL ? 1 : 0;
}

/* Open <base>/<name> through file.hp and read raw bytes into dst. */
static long hf_load(const char *name, char *dst, size_t cap) {
    error load_err = error();
    int path_len;
    long before_pos;
    long after_pos;
    size_t total_bytes = 0;
    int read_rc;
    path_len = snprintf(hf_fixture_path, sizeof(hf_fixture_path), "%s/%s",
                        hf_base_path, name);
    if (path_len <= 0 || (size_t)path_len >= sizeof(hf_fixture_path))
        return -1;
    {
        file load_handle = file(hf_fixture_path, "rb");
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
    if (argc > 1) {
        int base_len = snprintf(hf_base_path, sizeof(hf_base_path), "%s",
                                hf_argv_ptr(argv, 1));
        if (base_len <= 0 || (size_t)base_len >= sizeof(hf_base_path))
            return 2;
    } else {
        int base_len = snprintf(hf_base_path, sizeof(hf_base_path), "%s",
                                "tests/security/hostile_inputs/fixtures");
        if (base_len <= 0 || (size_t)base_len >= sizeof(hf_base_path))
            return 2;
    }

    /* ---- NULL / invalid length (no stream access needed) --------------- */
    {
        char probe_buf[16];
        error null_err = error();
        hf_check("io_token_null_buf", stdlib_io_token(NULL, 10) == 1);
        hf_check("io_token_zero_size", stdlib_io_token(probe_buf, 0) == 1);
        hf_check("io_read_line_null", read_line(NULL, 10, &null_err) ==
                                          STDLIB_FAIL &&
                 null_err.category() == STDLIB_CATEGORY_INVALID);
        hf_check("io_read_line_zero", read_line(probe_buf, 0, &null_err) ==
                                          STDLIB_FAIL &&
                 null_err.category() == STDLIB_CATEGORY_INVALID);
        hf_check("io_read_line_huge_size",
                 read_line(probe_buf, (size_t)INT_MAX + 1, &null_err) ==
                         STDLIB_FAIL &&
                 null_err.category() == STDLIB_CATEGORY_BOUNDS);
        hf_check("io_read_int_null", read_int(NULL, &null_err) == STDLIB_FAIL);
        hf_check("io_read_long_null", read_long(NULL, &null_err) == STDLIB_FAIL);
        hf_check("io_read_float_null", read_float(NULL, &null_err) == STDLIB_FAIL);
        hf_check("io_read_double_null", read_double(NULL, &null_err) == STDLIB_FAIL);
    }

    /* ---- signed overflow, negative values, non-finite floats ----------- */
    {
        int int_value = 0;
        error num_err = error();
        int switched = hf_switch_stdin("io_overflow_int.txt");
        int rc = switched ? read_int(&int_value, &num_err) : STDLIB_FAIL;
        hf_check("io_int_overflow", switched == 1 && rc == STDLIB_FAIL &&
                 num_err.category() == STDLIB_CATEGORY_RANGE);
    }
    {
        int neg_value = 0;
        error neg_err = error();
        int switched = hf_switch_stdin("io_negative_int.txt");
        int rc = switched ? read_int(&neg_value, &neg_err) : STDLIB_FAIL;
        hf_check("io_int_negative_int_min",
                 switched == 1 && rc == STDLIB_OK && neg_value == INT_MIN);
    }
    {
        long long_value = 0;
        error long_err = error();
        int switched = hf_switch_stdin("io_overflow_long.txt");
        int rc = switched ? read_long(&long_value, &long_err) : STDLIB_FAIL;
        hf_check("io_long_overflow", switched == 1 && rc == STDLIB_FAIL &&
                 long_err.category() == STDLIB_CATEGORY_RANGE);
    }
    {
        float float_value = 0.0f;
        error float_err = error();
        int switched = hf_switch_stdin("io_float_overflow.txt");
        int rc = switched ? read_float(&float_value, &float_err) : STDLIB_FAIL;
        hf_check("io_float_overflow", switched == 1 && rc == STDLIB_FAIL &&
                 float_err.category() == STDLIB_CATEGORY_RANGE);
    }
    {
        float nan_value = 0.0f;
        error nan_err = error();
        int switched = hf_switch_stdin("io_float_nan.txt");
        int rc = switched ? read_float(&nan_value, &nan_err) : STDLIB_FAIL;
        hf_check("io_float_nan_rejected", switched == 1 && rc == STDLIB_FAIL &&
                 nan_err.category() == STDLIB_CATEGORY_RANGE);
    }
    {
        double inf_value = 0.0;
        error inf_err = error();
        int switched = hf_switch_stdin("io_double_inf.txt");
        int rc = switched ? read_double(&inf_value, &inf_err) : STDLIB_FAIL;
        hf_check("io_double_inf_rejected", switched == 1 && rc == STDLIB_FAIL &&
                 inf_err.category() == STDLIB_CATEGORY_RANGE);
    }
    {
        int bad_value = 0;
        error bad_err = error();
        int switched = hf_switch_stdin("io_not_integer.txt");
        int rc = switched ? read_int(&bad_value, &bad_err) : STDLIB_FAIL;
        hf_check("io_not_integer", switched == 1 && rc == STDLIB_FAIL &&
                 bad_err.category() == STDLIB_CATEGORY_INVALID);
    }

    /* ---- truncation, unterminated input, delimiters, embedded NUL ------ */
    {
        char token_buf[64];
        int switched = hf_switch_stdin("io_truncated_token.txt");
        int rc = switched ? stdlib_io_token(token_buf, sizeof(token_buf)) : 1;
        hf_check("io_token_truncated", switched == 1 && rc == 2 &&
                 strlen(token_buf) == 63 && hf_byte(token_buf, 62) == 'x');
    }
    {
        char line_buf[16];
        error line_err = error();
        int switched = hf_switch_stdin("io_long_line.txt");
        int rc = switched ? read_line(line_buf, sizeof(line_buf), &line_err) : STDLIB_FAIL;
        hf_check("io_read_line_truncated", switched == 1 && rc == STDLIB_FAIL &&
                 line_err.category() == STDLIB_CATEGORY_BOUNDS &&
                 line_buf[0] == '\0');
    }
    {
        char line_buf[16];
        error line_err = error();
        int switched = hf_switch_stdin("io_unterminated.txt");
        int rc = switched ? read_line(line_buf, sizeof(line_buf), &line_err) : STDLIB_FAIL;
        hf_check("io_read_line_unterminated", switched == 1 && rc == STDLIB_OK &&
                 strcmp(line_buf, "abc") == 0);
    }
    {
        char line_buf[16];
        error line_err = error();
        int switched = hf_switch_stdin("io_repeated_crlf.txt");
        int rc = switched ? read_line(line_buf, sizeof(line_buf), &line_err) : STDLIB_FAIL;
        hf_check("io_read_line_repeated_crlf",
                 switched == 1 && rc == STDLIB_OK &&
                 strcmp(line_buf, "a") == 0);
    }
    {
        char token_buf[16];
        int switched = hf_switch_stdin("io_embedded_nul.txt");
        int rc = switched ? stdlib_io_token(token_buf, sizeof(token_buf)) : 1;
        hf_check("io_token_embedded_nul",
                 switched == 1 && rc == 0 && strlen(token_buf) == 1 &&
                 hf_byte(token_buf, 1) == 0 && hf_byte(token_buf, 2) == 'b');
    }

    /* ---- file.hp: invalid length, handles, seeks, lifecycle ------------ */
    {
        char bin_path[640];
        int path_len = snprintf(bin_path, sizeof(bin_path), "%s/%s",
                                hf_base_path, "file_binary_nul.bin");
        if (path_len > 0 && (size_t)path_len < sizeof(bin_path)) {
            char read_buf[32];
            error file_err = error();
            file bin_file = file(bin_path, "rb");
            hf_check("file_open_fixture", bin_file.is_open() == 1);
            hf_check("file_read_null_buf",
                     bin_file.read(NULL, 8, &file_err) == STDLIB_FAIL);
            hf_check("file_read_zero_size",
                     bin_file.read(read_buf, 0, &file_err) == STDLIB_FAIL);
            hf_check("file_read_line_null_buf",
                     bin_file.read_line(NULL, 8, &file_err) == STDLIB_FAIL);
            hf_check("file_read_line_zero_size",
                     bin_file.read_line(read_buf, 0, &file_err) == STDLIB_FAIL);
            hf_check("file_read_line_huge_size",
                     bin_file.read_line(read_buf, (size_t)INT_MAX + 1,
                                        &file_err) == STDLIB_FAIL &&
                     file_err.category() == STDLIB_CATEGORY_BOUNDS);
            hf_check("file_seek_negative",
                     bin_file.seek(-1, SEEK_SET, &file_err) == STDLIB_FAIL &&
                     file_err.category() == STDLIB_CATEGORY_IO);
            hf_check("file_seek_past_eof",
                     bin_file.seek(1000000, SEEK_SET, &file_err) == STDLIB_OK);
            hf_check("file_read_after_past_eof",
                     bin_file.read(read_buf, sizeof(read_buf), &file_err) ==
                     STDLIB_EOF);
            hf_check("file_write_bytes_null",
                     bin_file.write_bytes(NULL, 5, &file_err) == STDLIB_FAIL);
            hf_check("file_write_bytes_null_zero_size",
                     bin_file.write_bytes(NULL, 0, &file_err) == STDLIB_OK);
            hf_check("file_close_first",
                     bin_file.close(&file_err) == STDLIB_OK);
            hf_check("file_close_twice",
                     bin_file.close(&file_err) == STDLIB_FAIL &&
                     file_err.category() == STDLIB_CATEGORY_LIFECYCLE);
        } else {
            hf_check("file_open_fixture", 0);
        }
    }
    {
        error borrow_err = error();
        file borrowed = file_borrow(NULL);
        char borrow_buf[8];
        hf_check("file_borrow_null_not_open", borrowed.is_open() == 0);
        hf_check("file_borrow_null_read",
                 borrowed.read(borrow_buf, sizeof(borrow_buf), &borrow_err) ==
                 STDLIB_FAIL);
    }
    {
        error empty_err = error();
        error missing_err = error();
        file empty_file = file("", "rb");
        file missing_file = file("/definitely/no/such/dir/file", "rb");
        (void)empty_err;
        (void)missing_err;
        hf_check("file_empty_path_not_open", empty_file.is_open() == 0);
        hf_check("file_missing_path_not_open", missing_file.is_open() == 0);
    }
    {
        error temp_err = error();
        file temp_file = file_temp(&temp_err);
        char temp_buf[8];
        hf_check("file_temp_open", temp_file.is_open() == 1);
        hf_check("file_temp_write",
                 temp_file.write_bytes("ab", 2, &temp_err) == STDLIB_OK);
        hf_check("file_temp_seek_zero",
                 temp_file.seek(0, SEEK_SET, &temp_err) == STDLIB_OK);
        hf_check("file_temp_read_back",
                 temp_file.read(temp_buf, sizeof(temp_buf), &temp_err) ==
                     STDLIB_OK && temp_buf[0] == 'a' && temp_buf[1] == 'b');
        temp_file.drop();
    }
    {
        char bin_buf[16];
        long bin_count = hf_load("file_binary_nul.bin", bin_buf,
                                 sizeof(bin_buf));
        int bytes_ok = bin_count == 8;
        if (bytes_ok) {
            bytes_ok = hf_byte(bin_buf, 0) == 0x00 &&
                       hf_byte(bin_buf, 1) == 0x01 &&
                       hf_byte(bin_buf, 2) == 'A' &&
                       hf_byte(bin_buf, 3) == 0x00 &&
                       hf_byte(bin_buf, 4) == 0xff &&
                       hf_byte(bin_buf, 5) == '\n' &&
                       hf_byte(bin_buf, 6) == 'B' &&
                       hf_byte(bin_buf, 7) == 0x00;
        }
        hf_check("file_binary_nul_bytes", bytes_ok);
    }

    print("HOSTILE_IO_FILE", hf_checks, hf_failures);
    return hf_failures == 0 ? 0 : 1;
}

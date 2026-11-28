/* Hostile-input corpus: stdlib/fs.hp and stdlib/net.hp (Task 6 Phase 2).
 *
 * Self-checking: each case prints "<tag> <0|1>"; final line is
 * "HOSTILE_FS_NET <checks> <failures>". Exit 0 only when failures == 0.
 * Path probes stay inside the fixture directory (or use non-existent paths);
 * socket probes use loopback port 0 or deliberately bogus descriptors. No
 * probe writes outside this corpus and every handle is dropped deterministically.
 *
 * Build: ./build/c+ build <this file> --strict --sanitize=none -o <bin>
 * Run (from repo root): <bin> [fixture_dir]
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"
#include "stdlib/net.hp"

static int hn_checks = 0;
static int hn_failures = 0;

static void hn_check(const char *tag, int cond) {
    hn_checks++;
    if (!cond)
        hn_failures++;
    print(tag, cond ? 1 : 0);
}

static char hn_base_path[512];

static char *hn_argv_ptr(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        int base_len = snprintf(hn_base_path, sizeof(hn_base_path), "%s",
                                hn_argv_ptr(argv, 1));
        if (base_len <= 0 || (size_t)base_len >= sizeof(hn_base_path))
            return 2;
    } else {
        int base_len = snprintf(hn_base_path, sizeof(hn_base_path), "%s",
                                "tests/security/hostile_inputs/fixtures");
        if (base_len <= 0 || (size_t)base_len >= sizeof(hn_base_path))
            return 2;
    }

    /* ---- fs.hp: NULL paths --------------------------------------------- */
    {
        error null_err = error();
        error size_err = error();
        error rm_err = error();
        error rename_err = error();
        error mkdir_err = error();
        error temp_err = error();
        char tmp_path[640];
        hn_check("fs_exists_null", fs_exists(NULL) == 0);
        hn_check("fs_is_file_null", fs_is_file(NULL) == 0);
        hn_check("fs_is_dir_null", fs_is_dir(NULL) == 0);
        hn_check("fs_size_null", fs_size(NULL, &size_err) == -1 &&
                 size_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("fs_remove_null", fs_remove(NULL, &rm_err) == STDLIB_FAIL &&
                 rm_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("fs_rename_null_from",
                 fs_rename(NULL, "x", &rename_err) == STDLIB_FAIL &&
                 rename_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("fs_rename_null_to",
                 fs_rename("x", NULL, &rename_err) == STDLIB_FAIL &&
                 rename_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("fs_mkdir_null", fs_mkdir(NULL, &mkdir_err) == STDLIB_FAIL &&
                 mkdir_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("fs_mkdtemp_null",
                 fs_mkdtemp(NULL, sizeof(tmp_path), &temp_err) ==
                     STDLIB_FAIL &&
                 temp_err.category() == STDLIB_CATEGORY_INVALID);
        (void)null_err;
    }

    /* ---- fs.hp: malformed / rejected templates and paths --------------- */
    {
        char short_tmpl[8];
        char bad_tmpl[32];
        error tmpl_err = error();
        int short_len = snprintf(short_tmpl, sizeof(short_tmpl), "%s", "abc");
        int bad_len = snprintf(bad_tmpl, sizeof(bad_tmpl), "%s", "abcdefgh");
        hn_check("fs_mkdtemp_short_size",
                 short_len > 0 && fs_mkdtemp(short_tmpl, 6, &tmpl_err) ==
                                      STDLIB_FAIL &&
                 tmpl_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("fs_mkdtemp_bad_suffix",
                 bad_len > 0 && fs_mkdtemp(bad_tmpl, sizeof(bad_tmpl),
                                           &tmpl_err) == STDLIB_FAIL &&
                 tmpl_err.category() == STDLIB_CATEGORY_INVALID);
    }
    {
        char missing_path[640];
        int path_len = snprintf(missing_path, sizeof(missing_path),
                                "%s/definitely_missing", hn_base_path);
        error size_err = error();
        error rm_err = error();
        hn_check("fs_size_missing",
                 path_len > 0 && fs_size(missing_path, &size_err) == -1 &&
                 size_err.category() == STDLIB_CATEGORY_IO);
        hn_check("fs_remove_missing",
                 fs_remove(missing_path, &rm_err) == STDLIB_FAIL &&
                 rm_err.category() == STDLIB_CATEGORY_IO);
    }
    {
        error mkdir_err = error();
        hn_check("fs_mkdir_existing",
                 fs_mkdir(".", &mkdir_err) == STDLIB_FAIL &&
                 mkdir_err.category() == STDLIB_CATEGORY_IO);
        hn_check("fs_exists_empty_path", fs_exists("") == 0);
        hn_check("fs_is_dir_dot", fs_is_dir(".") == 1);
    }
    {
        char temp_tmpl[640];
        int tmpl_len = snprintf(temp_tmpl, sizeof(temp_tmpl), "%s/tmp_XXXXXX",
                                hn_base_path);
        error temp_err = error();
        if (tmpl_len > 0 && (size_t)tmpl_len < sizeof(temp_tmpl)) {
            int rc = fs_mkdtemp(temp_tmpl, sizeof(temp_tmpl), &temp_err);
            hn_check("fs_mkdtemp_valid", rc == STDLIB_OK);
            if (rc == STDLIB_OK) {
                hn_check("fs_mkdtemp_created_dir", fs_is_dir(temp_tmpl) == 1);
                hn_check("fs_mkdtemp_cleanup", rmdir(temp_tmpl) == 0);
            }
        } else {
            hn_check("fs_mkdtemp_valid", 0);
        }
    }
    {
        char long_path[5000];
        error long_err = error();
        memset(long_path, 'a', sizeof(long_path) - 1);
        long_path[sizeof(long_path) - 1] = '\0';
        hn_check("fs_long_path_exists", fs_exists(long_path) == 0);
        hn_check("fs_long_path_size", fs_size(long_path, &long_err) == -1 &&
                 long_err.category() == STDLIB_CATEGORY_IO);
    }

    /* ---- net.hp: invalid descriptors and negative values --------------- */
    {
        connection neg_conn = connection_borrow(-1);
        hn_check("net_conn_borrow_neg", neg_conn.is_valid() == 0 &&
                 neg_conn.descriptor() == -1);
        neg_conn.drop();
    }
    {
        connection std_conn = connection_borrow(1);
        hn_check("net_conn_borrow_stdout", std_conn.is_valid() == 0 &&
                 std_conn.descriptor() == 1);
        std_conn.drop();
    }
    {
        char bogus_buf[16];
        error bogus_err = error();
        connection bogus_conn = connection_borrow(999999);
        /* Metadata observation: is_valid() only checks the numeric range. */
        hn_check("net_conn_bogus_is_valid", bogus_conn.is_valid() == 1);
        hn_check("net_conn_bogus_read",
                 bogus_conn.read(bogus_buf, sizeof(bogus_buf), &bogus_err) ==
                     STDLIB_FAIL &&
                 bogus_err.category() == STDLIB_CATEGORY_IO);
        hn_check("net_conn_read_null_buf",
                 bogus_conn.read(NULL, sizeof(bogus_buf), &bogus_err) ==
                     STDLIB_FAIL &&
                 bogus_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("net_conn_read_zero_cap",
                 bogus_conn.read(bogus_buf, 0, &bogus_err) == STDLIB_FAIL &&
                 bogus_err.category() == STDLIB_CATEGORY_INVALID);
        hn_check("net_conn_write_null",
                 bogus_conn.write(NULL, 5, &bogus_err) == STDLIB_FAIL &&
                 bogus_err.category() == STDLIB_CATEGORY_INVALID);
        /* A zero-length write never issues a syscall: it reports success even
         * on a descriptor that is not open. Recorded as an observation. */
        hn_check("net_conn_zero_len_write",
                 bogus_conn.write(bogus_buf, 0, &bogus_err) == STDLIB_OK);
        hn_check("net_conn_bogus_write",
                 bogus_conn.write("x", 1, &bogus_err) == STDLIB_FAIL &&
                 bogus_err.category() == STDLIB_CATEGORY_IO);
        bogus_conn.drop();
        bogus_conn.drop();
        hn_check("net_conn_drop_twice", bogus_conn.descriptor() == -1 &&
                 bogus_conn.is_valid() == 0);
    }
    {
        listener neg_listener = listener(-1);
        hn_check("net_listener_negative_port", neg_listener.is_valid() == 0 &&
                 neg_listener.descriptor() == -1);
        neg_listener.drop();
    }
    {
        listener big_listener = listener(65536);
        hn_check("net_listener_port_too_large", big_listener.is_valid() == 0);
        big_listener.drop();
    }
    {
        listener zero_listener = listener(0);
        int port;
        hn_check("net_listener_port_zero", zero_listener.is_valid() == 1);
        port = zero_listener.bound_port();
        hn_check("net_listener_bound_port", port > 0);
        zero_listener.drop();
    }
    {
        listener bogus_listener = listener_borrow(999999);
        hn_check("net_listener_borrow_bogus_is_valid",
                 bogus_listener.is_valid() == 1);
        hn_check("net_listener_borrow_bogus_port",
                 bogus_listener.bound_port() == 0);
        bogus_listener.drop();
    }

    print("HOSTILE_FS_NET", hn_checks, hn_failures);
    return hn_failures == 0 ? 0 : 1;
}

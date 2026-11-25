/* One deterministic cross-module regression test for the modern C+ stdlib.
 *
 * The single program exercises the failure/ownership combinations assigned to
 * agent 2, in this order:
 *   1  read a PID-unique temp file into stdlib/string via file::write_bytes,
 *      file::read and stdlib/io helpers (read_line),
 *   2  parse the resulting buffer with stdlib/json (yyjson-backed),
 *   3  store parsed integers in stdlib/collections intmap and stdlib/vector,
 *   4  grow both containers while the strings are still allocated,
 *   5  drop the containers BEFORE the strings and confirm the strings survive,
 *   6  propagate a file error through stdlib/error and assert category + code,
 *   7  gate an open with fs_exists (present and absent paths),
 *   8  inject an allocation failure, then perform a second operation and
 *      verify recovery,
 *   9  repeat failed/valid cycles and verify allocator telemetry balance,
 *   10 verify no double release via allocator telemetry (idempotent drop).
 *
 * The temp file is unique per process and is removed at the end (cleanup is
 * asserted). Randomness, where used for growth keys, is a fixed-seed LCG, so
 * stdout is byte-for-byte deterministic. No fatal probe is executed, no stale
 * pointer is dereferenced, and no header is modified to force a pass.
 *
 * Telemetry note: the C+ lowering emits constructors for owned locals at the
 * START of the block that declares them.  Every owned local therefore lives
 * in an inner block that begins AFTER stdlib_telemetry_reset()/set_enabled(1),
 * and the final balance snapshot is taken BEFORE that block ends.  All counted
 * allocations thus have a matching counted free; no pre-window allocation is
 * freed inside the window (and no scope-exit auto-drop is counted either).
 *
 * Every check prints "<tag> <0|1>". The final line is
 * "CROSS_MODULE <checks> <failures>" and the exit code is 0 only when there
 * are zero failures. All owned locals have unique names because the C+ RAII
 * lowering tracks owners by name for a whole function.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/file.hp"
#include "stdlib/io.hp"
#include "stdlib/fs.hp"
#include "stdlib/json.hp"

static int cm_checks = 0;
static int cm_failures = 0;

static void cm_check(const char *tag, int condition) {
    cm_checks++;
    if (!condition)
        cm_failures++;
    print(tag, condition ? 1 : 0);
}

/* Fixed-seed LCG (Numerical Recipes constants). Unsigned wrap is defined. */
static unsigned int cm_lcg_state = 0x13579BDFu;

static void cm_lcg_reseed(unsigned int seed) {
    cm_lcg_state = seed;
}

static unsigned int cm_lcg_next(void) {
    cm_lcg_state = cm_lcg_state * 1664525u + 1013904223u;
    return cm_lcg_state;
}

int main(void) {
    stdlib_telemetry_stats cm_tm;
    stdlib_telemetry_stats cm_tm_pre;
    int cm_final_rc = 0;

    /* One telemetry epoch for the whole program, started before ANY stdlib
     * block is allocated (all owners are declared in the block below). */
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    {
        error cm_err = error();
        char cm_path[128];
        const char *cm_payload =
            "{\"a\":11,\"b\":22,\"c\":33,\"nested\":{\"d\":44},\"arr\":[55,66,77]}";
        const char *cm_missing = "/tmp/cplus_agent2_cross_module_no_such_path";
        int cm_pid = (int)getpid();
        char cm_seed_line[64];
        string cm_seed_text = string("");
        int cm_seed_status = -2;
        string cm_text = string("");
        int cm_gated = 0;
        FILE *cm_saved_stdin = stdin;
        FILE *cm_stream = NULL;
        char cm_io_line[512];
        string cm_io_text = string("");
        int cm_io_status = -2;
        json_view cm_root;
        json_view cm_view_a;
        json_view cm_view_b;
        json_view cm_view_c;
        json_view cm_view_nested;
        json_view cm_view_d;
        json_view cm_view_arr;
        json_view cm_view_e0;
        json_view cm_view_e1;
        json_view cm_view_e2;
        int cm_va = -1;
        int cm_vb = -1;
        int cm_vc = -1;
        int cm_vd = -1;
        int cm_ve0 = -1;
        int cm_ve1 = -1;
        int cm_ve2 = -1;
        intmap cm_map = intmap();
        vector cm_vec = vector();
        int cm_i = 0;
        int cm_grow_ok = 1;
        unsigned int cm_r = 0u;
        int cm_key = 0;
        int cm_val = 0;
        file cm_bad = file(NULL, NULL);
        char cm_bad_buf[8];
        int cm_bad_status = -2;
        string cm_victim = string("seed");
        vector cm_cycle = vector();
        int cm_cycle_i = 0;
        int cm_cycle_ok = 1;
        size_t cm_want = 0u;
        intmap cm_idem = intmap();

        snprintf(cm_path, sizeof(cm_path), "/tmp/cplus_agent2_cross_%d.json",
                 cm_pid);
        if (fs_exists(cm_path))
            fs_remove(cm_path, &cm_err);
        cm_check("cm_fs_clean_start", fs_exists(cm_path) == 0);

        /* ---- 1a: stdlib/io helper reads the deterministic stdin fixture - */
        memset(cm_seed_line, 0, sizeof(cm_seed_line));
        cm_seed_status = read_line(cm_seed_line, sizeof(cm_seed_line), &cm_err);
        cm_check("cm_io_stdin_line",
                 cm_seed_status == STDLIB_OK && cm_err.ok() == 1 &&
                     strcmp(cm_seed_line, "cm-stdin-seed-4242") == 0);
        cm_check("cm_io_into_string",
                 cm_seed_text.append(cm_seed_line) == 0 &&
                     cm_seed_text.equals("cm-stdin-seed-4242") == 1);

        /* ---- 1b: write the PID-unique file with file::write_bytes ------- */
        {
            file cm_writer = file(cm_path, "w");
            cm_check("cm_write_open", cm_writer.is_open() == 1);
            cm_check("cm_write_bytes",
                     cm_writer.write_bytes(cm_payload, strlen(cm_payload),
                                           &cm_err) == STDLIB_OK &&
                         cm_err.ok() == 1);
            cm_check("cm_write_flush",
                     cm_writer.flush(&cm_err) == STDLIB_OK && cm_err.ok() == 1);
            cm_check("cm_write_close",
                     cm_writer.close(&cm_err) == STDLIB_OK &&
                         cm_writer.is_open() == 0);
        }

        /* ---- 7: fs_exists gates the open; read the file into a string --- */
        cm_check("cm_fs_present", fs_exists(cm_path) == 1);
        cm_check("cm_fs_absent", fs_exists(cm_missing) == 0);
        if (fs_exists(cm_path)) {
            file cm_reader = file(cm_path, "r");
            char cm_chunk[512];
            int cm_read_status = -2;
            cm_gated = cm_reader.is_open();
            memset(cm_chunk, 0, sizeof(cm_chunk));
            cm_read_status =
                cm_reader.read(cm_chunk, sizeof(cm_chunk) - 1, &cm_err);
            cm_check("cm_gated_open", cm_gated == 1);
            cm_check("cm_file_read",
                     cm_read_status == STDLIB_OK && cm_err.ok() == 1);
            cm_check("cm_file_into_string",
                     cm_text.append(cm_chunk) == 0 &&
                         cm_text.equals(cm_payload) == 1);
            cm_reader.drop();
        } else {
            cm_check("cm_gated_open", 0);
            cm_check("cm_file_read", 0);
            cm_check("cm_file_into_string", 0);
        }

        /* ---- 1c: stdlib/io read_line on the same file, into a string ---- */
        cm_stream = fopen(cm_path, "rb");
        cm_check("cm_io_file_open", cm_stream != NULL);
        if (cm_stream != NULL) {
            memset(cm_io_line, 0, sizeof(cm_io_line));
            stdin = cm_stream;
            cm_io_status = read_line(cm_io_line, sizeof(cm_io_line), &cm_err);
            stdin = cm_saved_stdin;
            fclose(cm_stream);
        }
        cm_check("cm_io_file_read",
                 cm_io_status == STDLIB_OK && cm_err.ok() == 1);
        cm_check("cm_io_file_into_string",
                 cm_io_text.append(cm_io_line) == 0 &&
                     cm_io_text.equals(cm_payload) == 1 &&
                     cm_text.equals(cm_io_text.c_str()) == 1);

        /* ---- 2: parse the buffer with stdlib/json ----------------------- */
        {
            json cm_doc = json(cm_text.c_str(), &cm_err);
            cm_root = cm_doc.root();
            cm_view_a = cm_root.member("a");
            cm_view_b = cm_root.member("b");
            cm_view_c = cm_root.member("c");
            cm_view_nested = cm_root.member("nested");
            cm_view_d = cm_view_nested.member("d");
            cm_view_arr = cm_root.member("arr");
            cm_view_e0 = cm_view_arr.at(0);
            cm_view_e1 = cm_view_arr.at(1);
            cm_view_e2 = cm_view_arr.at(2);
            cm_check("cm_json_valid", cm_doc.valid() == 1 && cm_err.ok() == 1);
            cm_check("cm_json_root",
                     cm_root.valid() == 1 && cm_root.is_object() == 1 &&
                         cm_root.size() == 5);
            cm_check("cm_json_int_a",
                     cm_view_a.get_int(&cm_va, &cm_err) == STDLIB_OK &&
                         cm_va == 11);
            cm_check("cm_json_int_b",
                     cm_view_b.get_int(&cm_vb, &cm_err) == STDLIB_OK &&
                         cm_vb == 22);
            cm_check("cm_json_int_c",
                     cm_view_c.get_int(&cm_vc, &cm_err) == STDLIB_OK &&
                         cm_vc == 33);
            cm_check("cm_json_int_d",
                     cm_view_d.get_int(&cm_vd, &cm_err) == STDLIB_OK &&
                         cm_vd == 44);
            cm_check("cm_json_array_ints",
                     cm_view_arr.is_array() == 1 && cm_view_arr.size() == 3 &&
                         cm_view_e0.get_int(&cm_ve0, &cm_err) == STDLIB_OK &&
                         cm_view_e1.get_int(&cm_ve1, &cm_err) == STDLIB_OK &&
                         cm_view_e2.get_int(&cm_ve2, &cm_err) == STDLIB_OK &&
                         cm_ve0 == 55 && cm_ve1 == 66 && cm_ve2 == 77 &&
                         cm_err.ok() == 1);
            /* Views are never used after the owning document is dropped. */
            cm_doc.drop();
        }

        /* ---- 3: store parsed integers in intmap and vector -------------- */
        cm_check("cm_store_intmap",
                 cm_map.put(1, cm_va) == 0 && cm_map.put(2, cm_vb) == 0 &&
                     cm_map.put(3, cm_vc) == 0 && cm_map.put(4, cm_vd) == 0);
        cm_check("cm_store_vector",
                 cm_vec.push_back(cm_va) == 0 && cm_vec.push_back(cm_vb) == 0 &&
                     cm_vec.push_back(cm_vc) == 0 &&
                     cm_vec.push_back(cm_vd) == 0 &&
                     cm_vec.push_back(cm_ve0) == 0 &&
                     cm_vec.push_back(cm_ve1) == 0 &&
                     cm_vec.push_back(cm_ve2) == 0);
        cm_check("cm_store_sizes", cm_map.size() == 4 && cm_vec.size() == 7);

        /* ---- 4: grow both containers while the strings are allocated --- */
        stdlib_telemetry_snapshot(&cm_tm_pre);
        cm_lcg_reseed(0xC0FFEEu);
        for (cm_i = 0; cm_i < 48; cm_i++) {
            cm_r = cm_lcg_next();
            cm_key = 100 + (int)(cm_r % 500u);
            cm_val = (int)((cm_r >> 12) & 0xFFFFu);
            if (cm_map.put(cm_key, cm_val) != 0)
                cm_grow_ok = 0;
            if (cm_vec.push_back(cm_val) != 0)
                cm_grow_ok = 0;
        }
        stdlib_telemetry_snapshot(&cm_tm);
        cm_check("cm_growth_ok", cm_grow_ok == 1);
        cm_check("cm_growth_sizes",
                 cm_map.size() > 4 && cm_vec.size() == 55 &&
                     cm_vec.capacity() >= 64);
        cm_check("cm_growth_telemetry",
                 cm_tm.container_growths > cm_tm_pre.container_growths);
        cm_check("cm_growth_strings_alive",
                 cm_text.valid() == 1 && cm_text.equals(cm_payload) == 1 &&
                     cm_io_text.valid() == 1 && cm_seed_text.valid() == 1);

        /* ---- 5: drop containers before the strings --------------------- */
        cm_map.drop();
        cm_vec.drop();
        cm_check("cm_drop_containers",
                 cm_map.valid() == 0 && cm_vec.size() == 0 &&
                     cm_vec.capacity() == 0);
        cm_check("cm_strings_survive_containers",
                 cm_text.valid() == 1 && cm_text.equals(cm_payload) == 1 &&
                     cm_io_text.valid() == 1 &&
                     cm_io_text.equals(cm_payload) == 1 &&
                     cm_seed_text.valid() == 1);
        cm_text.drop();
        cm_io_text.drop();
        cm_seed_text.drop();
        cm_check("cm_drop_strings",
                 cm_text.valid() == 0 && cm_io_text.valid() == 0 &&
                     cm_seed_text.valid() == 0);

        /* ---- 6: file error propagated through stdlib/error ------------- */
        memset(cm_bad_buf, 0, sizeof(cm_bad_buf));
        cm_bad_status = cm_bad.read(cm_bad_buf, sizeof(cm_bad_buf), &cm_err);
        cm_check("cm_file_error_status", cm_bad_status == STDLIB_FAIL);
        cm_check("cm_file_error_category_code",
                 cm_err.failed_now() == 1 &&
                     cm_err.category() == STDLIB_CATEGORY_IO &&
                     cm_err.code() == (int)CPLUS_EXIT_IO);
        cm_check("cm_file_error_message",
                 strstr(cm_err.what(), "null handle") != NULL);
        cm_bad.drop();

        /* ---- 8: induced allocation failure, then successful 2nd op ----- */
        stdlib_telemetry_snapshot(&cm_tm_pre);
        stdlib_alloc_fail_at(1);
        cm_check("cm_oom_first", cm_victim.reserve(256) == -1);
        cm_check("cm_oom_atomic",
                 cm_victim.valid() == 1 && cm_victim.equals("seed") == 1 &&
                     cm_victim.capacity() == 5);
        stdlib_telemetry_snapshot(&cm_tm);
        cm_check("cm_oom_telemetry",
                 cm_tm.alloc_failures - cm_tm_pre.alloc_failures == 1);
        stdlib_alloc_fail_reset();
        cm_check("cm_oom_recovered",
                 cm_victim.reserve(256) == 0 && cm_victim.capacity() >= 256 &&
                     cm_victim.equals("seed") == 1);
        cm_victim.drop();

        /* ---- 9: repeated failed/valid cycles with telemetry balance ---- */
        stdlib_telemetry_snapshot(&cm_tm_pre);
        for (cm_cycle_i = 0; cm_cycle_i < 16; cm_cycle_i++) {
            cm_want = (size_t)(cm_cycle_i + 1) * 8u;
            stdlib_alloc_fail_at(1);
            if (cm_cycle.reserve(cm_want) != -1)
                cm_cycle_ok = 0;
            stdlib_alloc_fail_reset();
            if (cm_cycle.reserve(cm_want) != 0)
                cm_cycle_ok = 0;
        }
        cm_cycle.drop();
        stdlib_telemetry_snapshot(&cm_tm);
        cm_check("cm_cycles_ok", cm_cycle_ok == 1);
        cm_check("cm_cycles_balance",
                 cm_tm.alloc_count - cm_tm_pre.alloc_count == 16 &&
                     cm_tm.free_count - cm_tm_pre.free_count == 16 &&
                     cm_tm.alloc_failures - cm_tm_pre.alloc_failures == 16);

        /* ---- 10: no double release (idempotent drop + balance) --------- */
        for (cm_i = 0; cm_i < 8; cm_i++)
            cm_idem.put(cm_i, cm_i * 3);
        stdlib_telemetry_snapshot(&cm_tm_pre);
        cm_idem.drop();
        stdlib_telemetry_snapshot(&cm_tm);
        cm_check("cm_idem_first_drop",
                 cm_tm.free_count - cm_tm_pre.free_count >= 3 &&
                     cm_tm.ownership_violations ==
                         cm_tm_pre.ownership_violations);
        stdlib_telemetry_snapshot(&cm_tm_pre);
        cm_idem.drop();
        stdlib_telemetry_snapshot(&cm_tm);
        cm_check("cm_idem_second_drop",
                 cm_tm.free_count == cm_tm_pre.free_count &&
                     cm_tm.ownership_violations == 0 &&
                     cm_tm.lifecycle_failures == 0);

        /* Cleanup: the PID-unique temp file must be gone. */
        cm_check("cm_cleanup_remove",
                 fs_remove(cm_path, &cm_err) == STDLIB_OK && cm_err.ok() == 1);
        cm_check("cm_cleanup_absent", fs_exists(cm_path) == 0);

        stdlib_telemetry_snapshot(&cm_tm);
        cm_check("cm_global_balance",
                 cm_tm.alloc_count > 0 && cm_tm.alloc_count == cm_tm.free_count &&
                     cm_tm.ownership_violations == 0 &&
                     cm_tm.lifecycle_failures == 0);

        cm_final_rc = cm_failures == 0 ? 0 : 1;
    }

    stdlib_telemetry_set_enabled(0);
    print("CROSS_MODULE", cm_checks, cm_failures);
    return cm_final_rc;
}

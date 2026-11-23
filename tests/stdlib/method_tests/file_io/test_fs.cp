/* Per-method verification for stdlib/fs.hp.
 *
 * Prints exactly one "<header> <method> PASS|FAIL" line per public function.
 * All scratch paths live under one mkdtemp-created directory derived from
 * TMPDIR (falling back to /tmp) and are removed before exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"

static const char *method_tmpdir(void) {
    const char *d = getenv("TMPDIR");
    if (d != NULL && d[0] != '\0')
        return d;
    return "/tmp";
}

static void report(const char *header, const char *method, int ok) {
    print(header, method, ok ? "PASS" : "FAIL");
}

static int write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    int ok;
    if (f == NULL) return 0;
    ok = fputs(text, f) != EOF;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

int main(void) {
    error err = error();
    int failures = 0;
    int ok;
    char tmpl[256];
    char dtmpl[320];
    char base[320];
    char file_path[360];
    char dir_path[360];
    char missing[360];
    char src[360];
    char dst[360];
    char rem[360];
    char *dir;

    snprintf(tmpl, sizeof(tmpl), "%s/cplus_method_fs_XXXXXX", method_tmpdir());
    dir = mkdtemp(tmpl);
    if (dir == NULL) {
        print("fs setup FAIL");
        return 1;
    }
    snprintf(base, sizeof(base), "%s", dir);
    snprintf(file_path, sizeof(file_path), "%s/file.txt", base);
    snprintf(dir_path, sizeof(dir_path), "%s/subdir", base);
    snprintf(missing, sizeof(missing), "%s/no_such_entry", base);
    snprintf(src, sizeof(src), "%s/rename_src.txt", base);
    snprintf(dst, sizeof(dst), "%s/rename_dst.txt", base);
    snprintf(rem, sizeof(rem), "%s/remove_me.txt", base);

    if (!write_file(file_path, "12345")) {
        print("fs setup FAIL");
        rmdir(base);
        return 1;
    }

    /* 1. fs_exists */
    ok = fs_exists(file_path) && fs_exists(base) &&
         !fs_exists(missing) && !fs_exists(NULL);
    report("fs", "fs_exists", ok);
    if (!ok) failures++;

    /* 2. fs_is_file */
    ok = mkdir(dir_path, 0777) == 0;
    ok = ok && fs_is_file(file_path) && !fs_is_file(dir_path) &&
         !fs_is_file(missing) && !fs_is_file(NULL);
    report("fs", "fs_is_file", ok);
    if (!ok) failures++;

    /* 3. fs_is_dir */
    ok = fs_is_dir(dir_path) && !fs_is_dir(file_path) &&
         !fs_is_dir(missing) && !fs_is_dir(NULL);
    report("fs", "fs_is_dir", ok);
    if (!ok) failures++;

    /* 4. fs_size */
    ok = fs_size(file_path, &err) == 5 && err.ok();
    if (fs_size(NULL, &err) != -1 ||
        err.code() != CPLUS_EXIT_UNSUPPORTED) ok = 0;
    if (fs_size(missing, &err) != -1 ||
        err.category() != STDLIB_CATEGORY_IO) ok = 0;
    report("fs", "fs_size", ok);
    if (!ok) failures++;

    /* 5. fs_remove */
    ok = write_file(rem, "bye");
    if (ok) ok = fs_remove(rem, &err) == STDLIB_OK && !fs_exists(rem);
    if (fs_remove(NULL, &err) != STDLIB_FAIL ||
        err.code() != CPLUS_EXIT_UNSUPPORTED) ok = 0;
    if (fs_remove(missing, &err) != STDLIB_FAIL ||
        err.category() != STDLIB_CATEGORY_IO) ok = 0;
    report("fs", "fs_remove", ok);
    if (!ok) failures++;

    /* 6. fs_rename */
    ok = write_file(src, "moving");
    if (ok) {
        ok = fs_rename(src, dst, &err) == STDLIB_OK &&
             fs_exists(dst) && !fs_exists(src);
    }
    if (fs_rename(NULL, dst, &err) != STDLIB_FAIL ||
        fs_rename(src, NULL, &err) != STDLIB_FAIL) ok = 0;
    report("fs", "fs_rename", ok);
    if (!ok) failures++;

    /* 7. fs_mkdir */
    {
        char made[360];
        snprintf(made, sizeof(made), "%s/newdir", base);
        ok = fs_mkdir(made, &err) == STDLIB_OK && fs_is_dir(made);
        if (fs_mkdir(NULL, &err) != STDLIB_FAIL ||
            err.code() != CPLUS_EXIT_UNSUPPORTED) ok = 0;
        if (fs_mkdir(made, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_IO) ok = 0;
        report("fs", "fs_mkdir", ok);
        if (!ok) failures++;
        rmdir(made);
    }

    /* 8. fs_mkdtemp */
    {
        char bad[320];
        char *made_dir;
        snprintf(dtmpl, sizeof(dtmpl), "%s/tmp_XXXXXX", base);
        ok = fs_mkdtemp(dtmpl, sizeof(dtmpl), &err) == STDLIB_OK &&
             err.ok() && fs_is_dir(dtmpl) && strcmp(dtmpl, base) != 0;
        made_dir = strrchr(dtmpl, '/');
        ok = ok && made_dir != NULL && strcmp(made_dir + 1, "tmp_XXXXXX") != 0;
        if (fs_mkdtemp(NULL, sizeof(dtmpl), &err) != STDLIB_FAIL ||
            fs_mkdtemp(dtmpl, 3, &err) != STDLIB_FAIL) ok = 0;
        snprintf(bad, sizeof(bad), "%s/not_a_template", base);
        if (fs_mkdtemp(bad, sizeof(bad), &err) != STDLIB_FAIL) ok = 0;
        report("fs", "fs_mkdtemp", ok);
        if (!ok) failures++;
        rmdir(dtmpl);
    }

    /* cleanup */
    remove(file_path);
    remove(dst);
    remove(src);
    remove(rem);
    rmdir(dir_path);
    rmdir(base);

    print("fs TOTAL", 8 - failures, "/ 8 PASS");
    return failures == 0 ? 0 : 1;
}

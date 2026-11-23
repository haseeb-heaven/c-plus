/* Per-method verification for stdlib/file.hp.
 *
 * Prints exactly one "<header> <method> PASS|FAIL" line per public method.
 * All scratch files live in one mkdtemp-created directory derived from TMPDIR
 * (falling back to /tmp) and are removed before exit.  No user-specific paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static const char *method_tmpdir(void) {
    const char *d = getenv("TMPDIR");
    if (d != NULL && d[0] != '\0')
        return d;
    return "/tmp";
}

static void report(const char *header, const char *method, int ok) {
    print(header, method, ok ? "PASS" : "FAIL");
}

int main(void) {
    error err = error();
    int failures = 0;
    int ok;
    char tmpl[256];
    char path[320];
    char path2[320];
    char buf[64];
    char *dir;
    FILE *raw;

    snprintf(tmpl, sizeof(tmpl), "%s/cplus_method_file_XXXXXX", method_tmpdir());
    dir = mkdtemp(tmpl);
    if (dir == NULL) {
        print("file setup FAIL");
        return 1;
    }
    snprintf(path, sizeof(path), "%s/data.bin", dir);
    snprintf(path2, sizeof(path2), "%s/other.bin", dir);

    /* 1. file (constructor) */
    {
        file f = file(path, "w+");
        ok = f.is_open() && f.native() != NULL;
        report("file", "file", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 2. is_open */
    {
        file f = file(path, "w+");
        file none = file(NULL, NULL);
        ok = f.is_open() && !none.is_open();
        report("file", "is_open", ok);
        if (!ok) failures++;
        f.drop();
        none.drop();
    }

    /* 3. native */
    {
        file f = file(path, "w+");
        FILE *h = f.native();
        ok = h != NULL;
        if (ok) {
            if (fputs("Z", h) == EOF) ok = 0;
            if (ok) {
                if (fseek(h, 0, SEEK_SET) != 0) ok = 0;
            }
            if (ok) {
                int c = fgetc(h);
                if (c != 'Z') ok = 0;
            }
        }
        report("file", "native", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 4. read */
    {
        file f = file(path, "w+");
        int st1, st2, st3;
        ok = f.write_bytes("abcdef", 6, &err) == STDLIB_OK &&
             f.seek(0, SEEK_SET, &err) == STDLIB_OK;
        if (ok) {
            st1 = f.read(buf, 4, &err);
            ok = st1 == STDLIB_OK && memcmp(buf, "abcd", 4) == 0;
        }
        if (ok) {
            st2 = f.read(buf, 4, &err);
            ok = st2 == STDLIB_OK && memcmp(buf, "ef", 2) == 0;
        }
        if (ok) {
            st3 = f.read(buf, 4, &err);
            ok = st3 == STDLIB_EOF && err.ok();
        }
        report("file", "read", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 5. read_line */
    {
        file f = file(path, "w+");
        ok = f.write_bytes("alpha\nbeta\n", 11, &err) == STDLIB_OK &&
             f.seek(0, SEEK_SET, &err) == STDLIB_OK;
        if (ok) {
            ok = f.read_line(buf, sizeof(buf), &err) == STDLIB_OK &&
                 strcmp(buf, "alpha") == 0;
        }
        if (ok) {
            ok = f.read_line(buf, sizeof(buf), &err) == STDLIB_OK &&
                 strcmp(buf, "beta") == 0;
        }
        report("file", "read_line", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 6. write */
    {
        file f = file(path, "w+");
        ok = f.write("hello") != 0 &&
             f.seek(0, SEEK_SET, &err) == STDLIB_OK &&
             f.read_line(buf, sizeof(buf), &err) == STDLIB_OK &&
             strcmp(buf, "hello") == 0;
        report("file", "write", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 7. write_bytes */
    {
        file f = file(path, "w+");
        ok = f.write_bytes("XYZ", 3, &err) == STDLIB_OK &&
             f.seek(0, SEEK_SET, &err) == STDLIB_OK &&
             f.read(buf, 3, &err) == STDLIB_OK &&
             memcmp(buf, "XYZ", 3) == 0;
        report("file", "write_bytes", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 8. seek (success and bounds failure) */
    {
        file f = file(path, "w+");
        ok = f.write_bytes("abcde", 5, &err) == STDLIB_OK &&
             f.seek(2, SEEK_SET, &err) == STDLIB_OK &&
             f.read(buf, 1, &err) == STDLIB_OK && buf[0] == 'c' &&
             f.seek(-1, SEEK_SET, &err) == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_IO;
        report("file", "seek", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 9. flush (open and closed) */
    {
        file f = file(path, "w+");
        ok = f.write("flushme") != 0 &&
             f.flush(&err) == STDLIB_OK && err.ok();
        if (ok) {
            ok = f.close(&err) == STDLIB_OK &&
                 f.flush(&err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_IO;
        }
        report("file", "flush", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 10. close (success and double-close) */
    {
        file f = file(path, "w+");
        ok = f.is_open() && f.close(&err) == STDLIB_OK && !f.is_open();
        if (ok) {
            ok = f.close(&err) == STDLIB_FAIL &&
                 err.category() == STDLIB_CATEGORY_LIFECYCLE;
        }
        report("file", "close", ok);
        if (!ok) failures++;
        f.drop();
    }

    /* 11. drop (owned handle; idempotent) */
    {
        file f = file(path2, "w+");
        ok = f.is_open();
        f.drop();
        ok = ok && !f.is_open() && f.native() == NULL;
        f.drop();
        ok = ok && !f.is_open();
        report("file", "drop", ok);
        if (!ok) failures++;
    }

    /* 12. file_borrow (non-owning wrapper) */
    {
        raw = tmpfile();
        ok = raw != NULL;
        if (ok) {
            if (fputs("hi\n", raw) == EOF) ok = 0;
            if (ok) rewind(raw);
            if (ok) {
                file b = file_borrow(raw);
                ok = b.native() == raw && b.is_open() &&
                     b.read_line(buf, sizeof(buf), &err) == STDLIB_OK &&
                     strcmp(buf, "hi") == 0 &&
                     b.close(&err) == STDLIB_OK;
                if (ok) {
                    rewind(raw);
                    ok = fgetc(raw) == 'h';
                }
                if (fclose(raw) != 0) ok = 0;
            }
        }
        report("file", "file_borrow", ok);
        if (!ok) failures++;
    }

    /* 13. file_temp */
    {
        file t = file_temp(&err);
        ok = t.is_open() && t.native() != NULL && err.ok();
        if (ok) {
            ok = t.write_bytes("t", 1, &err) == STDLIB_OK &&
                 t.seek(0, SEEK_SET, &err) == STDLIB_OK &&
                 t.read(buf, 1, &err) == STDLIB_OK && buf[0] == 't';
        }
        report("file", "file_temp", ok);
        if (!ok) failures++;
        t.drop();
    }

    /* cleanup: everything created lives under dir */
    remove(path);
    remove(path2);
    rmdir(dir);

    print("file TOTAL", 13 - failures, "/ 13 PASS");
    return failures == 0 ? 0 : 1;
}

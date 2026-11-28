/* Per-method verification for stdlib/io.hp.
 *
 * Prints exactly one "<header> <method> PASS|FAIL" line per public function.
 * stdin is redirected to a generated fixture file and stdout is briefly
 * redirected to a capture file for println(), so results never depend on the
 * caller's terminal.  All scratch files live in one mkdtemp-created directory
 * derived from TMPDIR (falling back to /tmp) and are removed before exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

static const char *method_tmpdir(void) {
    const char *d = getenv("TMPDIR");
    if (d != NULL && d[0] != '\0')
        return d;
    return "/tmp";
}

static void report(const char *header, const char *method, int ok) {
    print(header, method, ok ? "PASS" : "FAIL");
}

/* println() is void: verify its observable result (a single newline). */
static int check_println(const char *cap_path) {
    int ok;
    int saved;
    FILE *cap;
    FILE *in;
    char b[8];
    size_t n;

    fflush(stdout);
    saved = dup(1);
    ok = saved >= 0;
    cap = NULL;
    if (ok) {
        cap = fopen(cap_path, "w");
        ok = cap != NULL;
    }
    if (ok) {
        if (dup2(fileno(cap), 1) < 0) ok = 0;
        if (ok) {
            println();
            fflush(stdout);
        }
        if (dup2(saved, 1) < 0) ok = 0;
        if (fclose(cap) != 0) ok = 0;
    }
    if (saved >= 0) close(saved);

    if (ok) {
        in = fopen(cap_path, "r");
        ok = in != NULL;
        if (ok) {
            n = fread(b, 1, sizeof(b), in);
            ok = n == 1 && b[0] == '\n';
            if (fclose(in) != 0) ok = 0;
        }
    }
    return ok;
}

int main(void) {
    error err = error();
    int failures = 0;
    char tmpl[256];
    char in_path[320];
    char cap_path[320];
    char *dir;
    FILE *make;
    FILE *in;
    int saved;
    int st;
    int ok;
    char tok[32];
    char line[64];
    int i = 0;
    long l = 0;
    float f = 0;
    double d = 0;
    int r_token = 0, r_line = 0, r_int = 0;
    int r_long = 0, r_float = 0, r_double = 0;

    snprintf(tmpl, sizeof(tmpl), "%s/cplus_method_io_XXXXXX", method_tmpdir());
    dir = mkdtemp(tmpl);
    if (dir == NULL) {
        print("io setup FAIL");
        return 1;
    }
    snprintf(in_path, sizeof(in_path), "%s/input.txt", dir);
    snprintf(cap_path, sizeof(cap_path), "%s/println.out", dir);

    /* Deterministic stdin fixture: token, line, int, long, float, double. */
    make = fopen(in_path, "w");
    if (make == NULL) {
        print("io setup FAIL");
        rmdir(dir);
        return 1;
    }
    if (fputs("tokenXYZ\nline one\n42\n9876543210\n3.5\n2.25\n", make) == EOF) {
        fclose(make);
        print("io setup FAIL");
        remove(in_path);
        rmdir(dir);
        return 1;
    }
    if (fclose(make) != 0) {
        print("io setup FAIL");
        remove(in_path);
        rmdir(dir);
        return 1;
    }

    /* 1. println */
    ok = check_println(cap_path);
    report("io", "println", ok);
    if (!ok) failures++;

    /* Redirect stdin to the fixture for the six readers. */
    in = fopen(in_path, "r");
    saved = in != NULL ? dup(0) : -1;
    if (in != NULL && saved >= 0 && dup2(fileno(in), 0) >= 0) {
        /* 2. stdlib_io_token */
        memset(tok, 0, sizeof(tok));
        st = stdlib_io_token(tok, sizeof(tok));
        r_token = st == 0 && strcmp(tok, "tokenXYZ") == 0;
        if (stdlib_io_token(NULL, sizeof(tok)) != 1) r_token = 0;

        /* 3. read_line */
        memset(line, 0, sizeof(line));
        st = read_line(line, sizeof(line), &err);
        r_line = st == STDLIB_OK && strcmp(line, "line one") == 0;
        if (read_line(NULL, sizeof(line), &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_INVALID) r_line = 0;

        /* 4. read_int */
        st = read_int(&i, &err);
        r_int = st == STDLIB_OK && i == 42;
        if (read_int(NULL, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_INVALID) r_int = 0;

        /* 5. read_long */
        st = read_long(&l, &err);
        r_long = st == STDLIB_OK && l == 9876543210L;
        if (read_long(NULL, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_INVALID) r_long = 0;

        /* 6. read_float */
        st = read_float(&f, &err);
        r_float = st == STDLIB_OK && f > 3.49f && f < 3.51f;
        if (read_float(NULL, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_INVALID) r_float = 0;

        /* 7. read_double */
        st = read_double(&d, &err);
        r_double = st == STDLIB_OK && d > 2.24 && d < 2.26;
        if (read_double(NULL, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_INVALID) r_double = 0;

        dup2(saved, 0);
    }
    if (saved >= 0) close(saved);
    if (in != NULL && fclose(in) != 0) {
        r_token = r_line = r_int = 0;
        r_long = r_float = r_double = 0;
    }

    report("io", "stdlib_io_token", r_token);
    report("io", "read_line", r_line);
    report("io", "read_int", r_int);
    report("io", "read_long", r_long);
    report("io", "read_float", r_float);
    report("io", "read_double", r_double);
    if (!r_token) failures++;
    if (!r_line) failures++;
    if (!r_int) failures++;
    if (!r_long) failures++;
    if (!r_float) failures++;
    if (!r_double) failures++;

    remove(in_path);
    remove(cap_path);
    rmdir(dir);

    print("io TOTAL", 7 - failures, "/ 7 PASS");
    return failures == 0 ? 0 : 1;
}

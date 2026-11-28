#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_runtime.h"
#include "safe_string.h"

static void expect_trap(void (*fn)(void *), void *ctx) {
    pid_t pid;
    int status = 0;
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        fn(ctx);
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 70);
}

/* Case 1: user-controlled format string in printf_s */
static void c1_printf_dyn(void *unused) {
    char input[64] = "%x %x %s";
    (void)unused;
    (void)printf_s(input);
}

/* Case 2: user-controlled format string in fprintf_s */
static void c2_fprintf_dyn(void *unused) {
    char input[64] = "%s%s%s";
    (void)unused;
    (void)fprintf_s(stderr, input);
}

/* Case 3: user-controlled format string in snprintf_s */
static void c3_snprintf_dyn(void *unused) {
    char input[64] = "%x %x %s";
    char out[128];
    (void)unused;
    (void)snprintf_s(out, sizeof(out), input);
}

/* Case 4: %n writing through fake pointer */
static void c4_percent_n_fake_ptr(void *unused) {
    uintptr_t fake_ptr = (uintptr_t)0xdeadbeefULL;
    (void)unused;
    (void)printf_s("Count: %n\n", (int *)(uintptr_t)fake_ptr);
}

/* Case 8: positional %999$s OOB stack read (refused even as literal) */
static void c8_positional_oob(void *unused) {
    (void)unused;
    (void)cplus_printf_s("%999$s", "hello");
}

/* Case 10: function pointer indirection still caught at runtime for %n / %999$s */
static void c10_fn_ptr_percent_n(void *unused) {
    int (*fn)(const char *, ...) = cplus_printf_s;
    int sink = 0;
    (void)unused;
    (void)fn("%n", &sink);
}

/* Case 13: fgets_s size > stack buffer capacity */
static void c13_fgets_overflow(void *unused) {
    char small[8];
    FILE *f = fopen_s("/tmp/cplus_25_fgets.tmp", "wb+");
    (void)unused;
    assert(f != NULL);
    (void)fwrite_s("0123456789ABCDEF", 16, 1, 16, f);
    rewind(f);
    (void)fgets_s(small, 64, f);
    (void)fclose_s(f);
}

/* Case 16: fread_s bmax > stack buffer capacity */
static void c16_fread_overflow(void *unused) {
    char small_buf[8];
    FILE *f = fopen_s("/tmp/cplus_25_fread.tmp", "wb+");
    (void)unused;
    assert(f != NULL);
    (void)fwrite_s("0123456789ABCDEF", 16, 1, 16, f);
    rewind(f);
    (void)fread_s(small_buf, 64, 1, 64, f);
    (void)fclose_s(f);
}

/* Case 20: aliased FILE* use-after-close */
static void c20_alias_use_after_close_fprintf(void *unused) {
    FILE *f1 = fopen_s("/tmp/cplus_25_uac.tmp", "wb+");
    FILE *f2 = f1;
    (void)unused;
    assert(f1 != NULL);
    assert(fclose_s(f1) == 0);
    f1 = NULL;
    (void)fprintf_s(f2, "use after close\n");
}

/* Case 20b: aliased FILE* use-after-close in fgets_s */
static void c20_alias_use_after_close_fgets(void *unused) {
    char buf[16];
    FILE *f1 = fopen_s("/tmp/cplus_25_uac2.tmp", "wb+");
    FILE *f2 = f1;
    (void)unused;
    assert(f1 != NULL);
    assert(fclose_s(f1) == 0);
    (void)fgets_s(buf, sizeof(buf), f2);
}

/* Case 21: fscanf_s unbounded %s */
static void c21_fscanf_unbounded_s(void *unused) {
    char word[8];
    FILE *f = fopen_s("/tmp/cplus_25_fscanf.tmp", "wb+");
    (void)unused;
    assert(f != NULL);
    (void)fprintf_s(f, "superlongtoken");
    rewind(f);
    (void)fscanf_s(f, "%s", word);
    (void)fclose_s(f);
}

typedef struct LeakCtx {
    FILE *f;
} LeakCtx;

static void close_stream_cleanup(void *p) {
    LeakCtx *ctx = (LeakCtx *)p;
    if (ctx && ctx->f) {
        (void)fclose_s(ctx->f);
        ctx->f = NULL;
    }
}

static int guard_early_abort_body(void *p) {
    LeakCtx *ctx = (LeakCtx *)p;
    ctx->f = fopen_s("/tmp/cplus_25_leak.tmp", "wb");
    assert(ctx->f != NULL);
    assert(cplus_runtime_push_cleanup(close_stream_cleanup, ctx) == 0);
    cplus_runtime_violation("guard_test", "simulated early failure");
    return 0;
}

static void check_compile_time_format_reject(const char *tag, const char *code) {
    char path[128];
    char cmd[320];
    const char *cc = getenv("CPLUS_CLANG");
    FILE *fp;
    int rc;
    if (!cc || !*cc) cc = "clang";
    snprintf(path, sizeof(path), "/tmp/cplus_25_ct_%s.c", tag);
    fp = fopen(path, "w");
    assert(fp != NULL);
    fputs("#include <stdio.h>\n#include \"safe_io.h\"\n", fp);
    fputs(code, fp);
    fclose(fp);
    snprintf(cmd, sizeof(cmd),
             "%s -fsyntax-only -std=c99 -Werror=format -Werror=format-security -Istdlib/safe %s >/dev/null 2>&1",
             cc, path);
    rc = system(cmd);
    unlink(path);
    assert(rc != 0);
}

int main(void) {
    /* Cases 1-4, 8, 10, 13, 16, 20, 21 fail-stop runtime traps */
    expect_trap(c1_printf_dyn, NULL);
    expect_trap(c2_fprintf_dyn, NULL);
    expect_trap(c3_snprintf_dyn, NULL);
    expect_trap(c4_percent_n_fake_ptr, NULL);
    expect_trap(c8_positional_oob, NULL);
    expect_trap(c10_fn_ptr_percent_n, NULL);
    expect_trap(c13_fgets_overflow, NULL);
    unlink("/tmp/cplus_25_fgets.tmp");
    expect_trap(c16_fread_overflow, NULL);
    unlink("/tmp/cplus_25_fread.tmp");
    expect_trap(c20_alias_use_after_close_fprintf, NULL);
    unlink("/tmp/cplus_25_uac.tmp");
    expect_trap(c20_alias_use_after_close_fgets, NULL);
    unlink("/tmp/cplus_25_uac2.tmp");
    expect_trap(c21_fscanf_unbounded_s, NULL);
    unlink("/tmp/cplus_25_fscanf.tmp");

    /* Cases 5, 6, 7, 22 compile-time format attribute mismatches */
    if (system("command -v clang >/dev/null 2>&1") == 0 &&
        access("stdlib/safe/safe_io.h", R_OK) == 0) {
        check_compile_time_format_reject("5_missing_arg",
            "int main(void){ cplus_printf_s(\"ID=%d Name=%s\\n\", 42); return 0; }\n");
        check_compile_time_format_reject("6_int_for_s",
            "int main(void){ int c = 1; cplus_printf_s(\"Code: %s\\n\", c); return 0; }\n");
        check_compile_time_format_reject("7_swapped_star_s",
            "int main(void){ const char *n=\"a\"; int w=4; cplus_printf_s(\"%*s\\n\", n, w); return 0; }\n");
        check_compile_time_format_reject("22_sscanf_missing_amp",
            "int main(void){ int num=0; cplus_sscanf_s(\"12\", \"%d\", num); return 0; }\n");
    }

    /* Case 9: Non-NUL buffer bounded check + %.4s precision formatting */
    {
        char tag[4] = {'A', 'B', 'C', 'D'};
        char out[32];
        assert(cplus_strnlen_s(tag, sizeof(tag)) == sizeof(tag));
        assert(snprintf_s(out, sizeof(out), "Tag: %.4s", tag) == 9);
        assert(strcmp(out, "Tag: ABCD") == 0);
    }

    /* Case 11 & 12: fgets_s normal line read & EOF zero-termination */
    {
        char buf[32];
        FILE *f = fopen_s("/tmp/cplus_25_eof.tmp", "wb+");
        assert(f != NULL);
        memset(buf, 'X', sizeof(buf));
        assert(fgets_s(buf, (int)sizeof(buf), f) == NULL);
        assert(buf[0] == '\0');
        assert(fprintf_s(f, "hello\n") == 6);
        rewind(f);
        assert(fgets_s(buf, (int)sizeof(buf), f) == buf);
        assert(strcmp(buf, "hello\n") == 0);
        assert(fclose_s(f) == 0);
        unlink("/tmp/cplus_25_eof.tmp");
    }

    /* Case 14: fgets_s(buf, sizeof(buf), NULL) does not crash */
    {
        char buf[16];
        errno = 0;
        assert(fgets_s(buf, (int)sizeof(buf), NULL) == NULL && errno == EINVAL);
    }

    /* Case 15: fread_s size * count overflow */
    {
        char buf[64];
        FILE *f = fopen_s("/tmp/cplus_25_ovf.tmp", "wb+");
        size_t huge = (size_t)-1 / 4u + 10u;
        assert(f != NULL);
        errno = 0;
        assert(fread_s(buf, sizeof(buf), 4u, huge, f) == 0 && errno == EOVERFLOW);
        assert(fclose_s(f) == 0);
        unlink("/tmp/cplus_25_ovf.tmp");
    }

    /* Case 17: fread_s partial element count read */
    {
        int records[4] = {0, 0, 0, 0};
        int src[3] = {10, 20, 30};
        FILE *f = fopen_s("/tmp/cplus_25_part.tmp", "wb+");
        assert(f != NULL);
        assert(fwrite_s(src, sizeof(src), sizeof(int), 3, f) == 3);
        rewind(f);
        assert(fread_s(records, sizeof(records), sizeof(records), 1, f) == 0);
        rewind(f);
        assert(fread_s(records, sizeof(records), sizeof(int), 4, f) == 3);
        assert(records[0] == 10 && records[1] == 20 && records[2] == 30 && records[3] == 0);
        assert(fclose_s(f) == 0);
        unlink("/tmp/cplus_25_part.tmp");
    }

    /* Case 18: fopen_s failure + NULL fprintf_s / fclose_s */
    {
        FILE *f = fopen_s("/nonexistent_dir_cplus_25/file.txt", "r");
        assert(f == NULL);
        errno = 0;
        assert(fprintf_s(f, "data\n") == -1 && errno == EINVAL);
        assert(fclose_s(f) == EOF);
    }

    /* Case 19: stream leak detection + RAII-style guard cleanup */
    {
        LeakCtx ctx;
        size_t before = cplus_safe_open_stream_count();
        ctx.f = NULL;
        assert(cplus_runtime_guard(guard_early_abort_body, &ctx) == CPLUS_RUNTIME_ERROR);
        assert(ctx.f == NULL);
        assert(cplus_safe_open_stream_count() == before);
        unlink("/tmp/cplus_25_leak.tmp");
    }

    /* Case 21b: bounded fscanf_s succeeds */
    {
        char word[8] = {0};
        FILE *f = fopen_s("/tmp/cplus_25_scanok.tmp", "wb+");
        assert(f != NULL);
        assert(fprintf_s(f, "superlongtoken") > 0);
        rewind(f);
        assert(fscanf_s(f, "%7s", word) == 1);
        assert(strcmp(word, "superlo") == 0);
        assert(fclose_s(f) == 0);
        unlink("/tmp/cplus_25_scanok.tmp");
    }

    /* Case 23: getenv_s copies value so setenv mutations cannot dangle it */
    {
        char copy[64];
        size_t len = 0;
        int i;
        assert(setenv("CPLUS_APP_KEY", "initial", 1) == 0);
        assert(getenv_s(&len, copy, sizeof(copy), "CPLUS_APP_KEY") == 0);
        assert(len == 7 && strcmp(copy, "initial") == 0);
        for (i = 0; i < 100; i++) {
            char k[32];
            snprintf(k, sizeof(k), "CPLUS_FILL_%d", i);
            assert(setenv(k, "reallocates_environ_table", 1) == 0);
        }
        assert(strcmp(copy, "initial") == 0);
    }

    /* Case 24: cplus_strerror_s caller buffers stay independent */
    {
        char e1[128], e2[128];
        assert(cplus_strerror_s(e1, sizeof(e1), ENOENT) == 0);
        assert(cplus_strerror_s(e2, sizeof(e2), ENOMEM) == 0);
        assert(strcmp(e1, e2) != 0);
    }

    /* Case 25: snprintf_s reports would-be length without OOB indexing */
    {
        char buf[8];
        int needed = snprintf_s(buf, sizeof(buf), "This string exceeds 8 bytes");
        assert(needed == 27);
        assert((size_t)needed >= sizeof(buf));
        assert(buf[sizeof(buf) - 1] == '\0');
    }

    /* Extra SafeCRT I/O & getenv_s edge-branch coverage */
    {
        char *hbuf = (char *)malloc_s(8);
        char tiny[4];
        size_t len = 99;
        FILE *f = fopen_s("/tmp/cplus_25_io_cov.tmp", "wb+");
        assert(hbuf != NULL && f != NULL);
        assert(cplus_safe_stream_was_closed(f) == 0);
        assert(fwrite_s(NULL, 8, 1, 1, f) == 0);
        assert(fwrite_s(hbuf, 8, 4, (size_t)-1 / 4u + 10u, f) == 0 && errno == EOVERFLOW);
        assert(fread_s(NULL, 8, 1, 1, f) == 0);
        assert(cplus_fgets_s(hbuf, 8, 8, f) == NULL);
        assert(hbuf[0] == '\0');
        assert(cplus_fgets_s(hbuf, 8, 0, f) == NULL && errno == EINVAL);
        assert(fclose_s(f) == 0);
        assert(cplus_safe_stream_was_closed(f) == 1);
        unlink("/tmp/cplus_25_io_cov.tmp");

        assert(cplus_getenv_s(&len, tiny, sizeof(tiny), "CPLUS_NONEXISTENT_ENV_XYZ") == 0 && len == 0 && tiny[0] == '\0');
        assert(cplus_getenv_s(&len, NULL, 4, "PATH") == EINVAL);
        assert(cplus_getenv_s(&len, tiny, sizeof(tiny), NULL) == EINVAL);
        assert(cplus_getenv_s(&len, tiny, sizeof(tiny), "CPLUS_APP_KEY") == ERANGE && tiny[0] == '\0');

        cplus_runtime_set_violation_trap(0);
        assert(fwrite_s(tiny, 16, 1, 16, stdout) == 0 && errno == ERANGE);
        free_s(hbuf);
        assert(fgets_s(hbuf, 8, stdin) == NULL && errno == EINVAL);
        assert(fread_s(hbuf, 8, 1, 8, stdin) == 0 && errno == EINVAL);
        assert(fwrite_s(hbuf, 8, 1, 8, stdout) == 0 && errno == EINVAL);
        assert(cplus_getenv_s(&len, hbuf, 8, "CPLUS_APP_KEY") == EINVAL);
        cplus_runtime_set_violation_trap(1);
    }

    puts("unit 25_stdio_safety_cases: ok");
    return 0;
}

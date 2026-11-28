/*
 * harness.c -- RED20 format red-team harness (agent 6/20).
 *
 * Targets: stdlib/safe/safe_format.h | stdlib/safe/src/safe_format.c
 *          stdlib/safe/safe_io.h     | stdlib/safe/src/safe_io.c
 *
 * Every case runs in a FORKED CHILD.  The child performs one hostile call,
 * checks canaries / %n write sentinels, writes a small Result record to a
 * pipe, then _exit()s.  The parent classifies signals as CRASH and uses the
 * child's record otherwise.
 *
 * Classes (precedence CRASH > CORRUPT > UNEXPECTED_OK > REFUSED):
 *   REFUSED       - hostile input rejected / no violation observed.
 *   CRASH         - child died from a signal or never reported.
 *   CORRUPT       - a canary past the declared capacity was modified.
 *   UNEXPECTED_OK - a guard was bypassed (e.g. a positional %n write landed
 *                   or a capacity lie let a write reach an adjacent object).
 *
 * Summary: RED20 format <cases> <crash> <corrupt> <unexpected_ok> <refused>
 * Exit 0 when the run completes (findings are reported, never hidden).
 *
 * Build variants (run.sh):
 *   format-first : safe_format.h first -> CPLUS_SAFE_OBJ_SIZE = BOS(p,1)
 *   io-first     : safe_io.h first     -> CPLUS_SAFE_OBJ_SIZE = BOS(p,0)
 */
#define _POSIX_C_SOURCE 200112L
/* Do NOT define CPLUS_SAFE_NO_UNPREFIXED_MACROS: we need the macro guards. */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

#ifdef RED20_OBJ_TYPE
#define CPLUS_SAFE_OBJ_SIZE(p) __builtin_object_size((p), RED20_OBJ_TYPE)
#endif
#ifdef RED20_IO_FIRST
#include "safe_io.h"
#include "safe_format.h"
#else
#include "safe_format.h"
#include "safe_io.h"
#endif
#include "safe_alloc.h"

extern void cplus_runtime_set_violation_trap(int);

/* variadic helper: va_start needs a variadic function */
static int vsnprintf_attack(char *dest, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = cplus_vsnprintf_s(dest, n, fmt, ap);
    va_end(ap);
    return rc;
}

/* ---------------- result protocol ---------------- */
enum { R_REFUSED = 0, R_CORRUPT = 1, R_UNEXPECTED_OK = 2, R_CRASH = 3 };

typedef struct {
    uint32_t magic;
    int32_t  cls;
    int32_t  api;
    int32_t  variant;
    int32_t  ret;
    int32_t  err;
    uint32_t sentinel;
    uint32_t canary;
    char     note[64];
} Result;

#define R_MAGIC 0x52453230u /* "RE20" */

static int g_result_fd = -1;
static volatile uint32_t g_target32;

static void report(int cls, int api, int variant, int rc,
                   uint32_t sentinel, uint32_t canary, const char *note)
{
    Result r;
    memset(&r, 0, sizeof r);
    r.magic = R_MAGIC;
    r.cls = cls;
    r.api = api;
    r.variant = variant;
    r.ret = rc;
    r.err = errno;
    r.sentinel = sentinel;
    r.canary = canary;
    if (note) snprintf(r.note, sizeof r.note, "%s", note);
    if (g_result_fd >= 0) {
        ssize_t ignored = write(g_result_fd, &r, sizeof r);
        (void)ignored;
    }
    _exit(0);
}

/* ---------------- canary buffers ---------------- */
typedef struct {
    unsigned char pre[16];
    unsigned char buf[64];
    unsigned char post[16];
} Guarded;

typedef struct {
    char member[12];
    char trail[16];
} MemberBuf;

#define CAN_PRE  1u
#define CAN_POST 2u
#define CAN_MEMB 4u

static void guarded_init(Guarded *g) {
    memset(g->pre,  0xA5, sizeof g->pre);
    memset(g->buf,  0x5A, sizeof g->buf);
    memset(g->post, 0x3C, sizeof g->post);
}
static uint32_t guarded_check(const Guarded *g) {
    uint32_t bad = 0;
    for (size_t i = 0; i < sizeof g->pre; ++i)  if (g->pre[i]  != 0xA5) bad |= CAN_PRE;
    for (size_t i = 0; i < sizeof g->post; ++i) if (g->post[i] != 0x3C) bad |= CAN_POST;
    return bad;
}

/* ---------------- format tables ---------------- */
/* CONFIRMED BYPASS class: length modifier BEFORE positional index.
 * parse_pos_dollar() is only called right after '%' (or after '*'), so the
 * "1$" here is skipped as a literal; libc still treats the trailing 'n' as
 * a positional %n conversion and writes through the supplied pointer. */
static const char *const PN_LEN_BEFORE[] = {
    "%h1$n", "%hh1$n", "%l1$n", "%ll1$n", "%z1$n", "%t1$n", "%j1$n", "%L1$n",
    "%h2$n", "%hh2$n", "%l2$n", "%ll2$n", "%z2$n", "%t2$n", "%j2$n", "%L2$n",
    "%h3$n", "%hh3$n", "%l3$n", "%ll3$n", "%z3$n", "%t3$n", "%j3$n", "%L3$n",
    "%h10$n","%hh10$n","%l10$n","%ll10$n","%z10$n","%t10$n","%j10$n","%L10$n"
};
static const char *const PN_POS_FIRST[] = {
    "%1$n", "%2$n", "%1$hn", "%1$hhn", "%1$ln", "%1$lln", "%1$zn", "%1$jn",
    "%1$*2$n", "%1$*n", "%1$-n", "%1$+n", "%1$ n", "%1$#n", "%1$0n", "%1$'n",
    "%1$.3n", "%1$*2$.*3$n", "%n", "%hhn", "%lln", "%zn", "%jn", "%Ln",
    "%*n", "%.*n", "%1$-10n", "%1$010n", "%1$'10n", "%1$10.4n", "%%n", "%%%n"
};
static const char *const PN_FLAG_WIDTH[] = {
    "%In", "%I'n", "%'In", "%I10n", "%I*n", "%I.3n", "%I.*n", "%lIn",
    "%Iln", "%hhIn", "%IIn", "%'n", "%I1$n", "%'1$n", "%I*1$n", "%I1$*2$n"
};
static const char *const SCAN_FMT[] = {
    "%s", "%[a-z]", "%[^a-z]", "%[]a]", "%1$s", "%1$[a-z]", "%0s", "%00s",
    "%*s", "%1$*s", "%*1$s", "%2$*1$s", "%1$*[a-z]", "%s%s", "%s%7s",
    "%7s%s", "%999999999s", "%[", "%[]", "%[^]", "%ls", "%[0-9A-Za-z]",
    "%S", "%c%s", "%d%s", "%n", "%1$n", "%h1$n", "%1$hn", "%hh1$n",
    "%ll1$n", "%z1$n", "%t1$n", "%j1$n", "%L1$n", "%*n", "%.*n", "%1$*2$n",
    "%7[a-z]", "%*[a-z]"
};

/* ---------------- API ids ---------------- */
enum {
    A_SNPRINTF_RAW = 0, A_SNPRINTF_CHK, A_SPRINTF_RAW, A_SPRINTF_CHK,
    A_PRINTF_RAW, A_FPRINTF_RAW, A_VSNPRINTF_RAW,
    A_SSCANF_RAW, A_SSCANF_CHK, A_SCANF_RAW, A_FSCANF_RAW,
    A_FGETS_CHK, A_FREAD_CHK, A_FWRITE_CHK,
    A_FCLOSE, A_FPRINTF_CLOSED, A_FWRITE_CLOSED,
    A_SNPRINTF_MACRO_MEMBER, A_FGETS_MACRO_MEMBER,
    A_NAPIS
};

static const char *api_name(int a) {
    static const char *const n[A_NAPIS] = {
        "snprintf_s", "snprintf_s_checked", "sprintf_s", "sprintf_s_checked",
        "printf_s", "fprintf_s", "vsnprintf_s", "sscanf_s", "sscanf_s_checked",
        "scanf_s", "fscanf_s", "fgets_s_checked", "fread_s_checked",
        "fwrite_s_checked", "fclose_s", "fprintf_s(closed)", "fwrite_s(closed)",
        "snprintf_s(macro/member)", "fgets_s(macro/member)"
    };
    return (a >= 0 && a < A_NAPIS) ? n[a] : "?";
}

#define PN_APIS 7
static const int PN_API_IDS[PN_APIS] = {
    A_SNPRINTF_RAW, A_SNPRINTF_CHK, A_SPRINTF_RAW, A_SPRINTF_CHK,
    A_PRINTF_RAW, A_FPRINTF_RAW, A_VSNPRINTF_RAW
};

/* ---------------- printf %n attack ---------------- */
static void do_pn(int idx)
{
    int api = PN_API_IDS[idx % PN_APIS];
    int v = idx / PN_APIS;
    const char *fmt;
    if (v < (int)(sizeof PN_LEN_BEFORE / sizeof PN_LEN_BEFORE[0]))
        fmt = PN_LEN_BEFORE[v];
    else if (v < (int)(sizeof PN_LEN_BEFORE / sizeof PN_LEN_BEFORE[0]) +
                 (int)(sizeof PN_POS_FIRST / sizeof PN_POS_FIRST[0]))
        fmt = PN_POS_FIRST[v - (int)(sizeof PN_LEN_BEFORE / sizeof PN_LEN_BEFORE[0])];
    else
        fmt = PN_FLAG_WIDTH[(v - (int)(sizeof PN_LEN_BEFORE / sizeof PN_LEN_BEFORE[0]) -
                             (int)(sizeof PN_POS_FIRST / sizeof PN_POS_FIRST[0])) %
                            (int)(sizeof PN_FLAG_WIDTH / sizeof PN_FLAG_WIDTH[0])];

    Guarded g;
    guarded_init(&g);
    g_target32 = 0x11223344u;
    errno = 0;
    int rc = -999;
    FILE *nullf = fopen("/dev/null", "w");
    switch (api) {
    case A_SNPRINTF_RAW:
        rc = cplus_snprintf_s((char *)g.buf, sizeof g.buf, fmt, &g_target32); break;
    case A_SNPRINTF_CHK:
        rc = cplus_snprintf_s_checked(1, sizeof g.buf, (char *)g.buf,
                                      sizeof g.buf, fmt, &g_target32); break;
    case A_SPRINTF_RAW:
        rc = cplus_sprintf_s((char *)g.buf, sizeof g.buf, fmt, &g_target32); break;
    case A_SPRINTF_CHK:
        rc = cplus_sprintf_s_checked(1, sizeof g.buf, (char *)g.buf,
                                     sizeof g.buf, fmt, &g_target32); break;
    case A_PRINTF_RAW:
        rc = cplus_printf_s(fmt, &g_target32); break;
    case A_FPRINTF_RAW:
        rc = cplus_fprintf_s(nullf, fmt, &g_target32); break;
    case A_VSNPRINTF_RAW:
        rc = vsnprintf_attack((char *)g.buf, sizeof g.buf, fmt, &g_target32);
        break;
    default: break;
    }
    if (nullf) fclose(nullf);
    uint32_t bad = guarded_check(&g);
    int cls = bad ? R_CORRUPT :
              (g_target32 != 0x11223344u ? R_UNEXPECTED_OK : R_REFUSED);
    report(cls, api, v, rc, (uint32_t)g_target32, bad, fmt);
}

/* ---------------- scanf attack ---------------- */
static void do_scan(int idx)
{
    int apis[4] = { A_SSCANF_RAW, A_SSCANF_CHK, A_SCANF_RAW, A_FSCANF_RAW };
    int api = apis[idx % 4];
    int v = idx / 4;
    const char *fmt = SCAN_FMT[v % (sizeof SCAN_FMT / sizeof SCAN_FMT[0])];
    Guarded g;
    guarded_init(&g);
    g_target32 = 0x11223344u;
    errno = 0;
    int rc = -999;
    switch (api) {
    case A_SSCANF_RAW:
        rc = cplus_sscanf_s("4 ABCDEFGHIJKLMNOP 12345", fmt, g.buf, &g_target32); break;
    case A_SSCANF_CHK:
        rc = cplus_sscanf_s_checked(1, "4 ABCDEFGHIJKLMNOP 12345", fmt,
                                    g.buf, &g_target32); break;
    case A_SCANF_RAW:
        rc = cplus_scanf_s(fmt, g.buf, &g_target32); break;
    case A_FSCANF_RAW: {
        FILE *f = tmpfile();
        if (f) { fputs("4 ABCDEFGHIJKLMNOP 12345\n", f); rewind(f); }
        rc = cplus_fscanf_s(f, fmt, g.buf, &g_target32);
        if (f) fclose(f);
        break;
    }
    default: break;
    }
    uint32_t bad = guarded_check(&g);
    int cls = bad ? R_CORRUPT :
              (g_target32 != 0x11223344u ? R_UNEXPECTED_OK : R_REFUSED);
    report(cls, api, v, rc, (uint32_t)g_target32, bad, fmt);
}

/* ---------------- capacity attacks ---------------- */
static void do_capacity(int idx)
{
    int apis[6] = { A_SNPRINTF_RAW, A_SNPRINTF_CHK, A_SPRINTF_RAW,
                    A_SPRINTF_CHK, A_FGETS_CHK, A_FREAD_CHK };
    int api = apis[(idx / 30) % 6];
    int buftype = (idx / 5) % 6;
    int delta = idx % 5;   /* 0..4; 1 == "one byte too small" */
    Guarded g; guarded_init(&g);
    MemberBuf m; memset(&m, 0x5A, sizeof m);
    errno = 0;
    int rc = -999;
    char *dest = NULL;
    size_t cap = 0;
    void *heap = NULL;
    switch (buftype) {
    case 0: dest = (char *)g.buf; cap = sizeof g.buf; break;
    case 1: dest = m.member;     cap = sizeof m.member; break;
    case 2: heap = malloc_s(24); dest = (char *)heap; cap = 24; break;
    case 3: dest = (char *)g.buf + 1; cap = sizeof g.buf - 1; break;
    case 4: dest = (char *)g.buf; cap = 8; break;
    default: dest = (char *)g.buf; cap = sizeof g.buf; break;
    }
    if (!dest) report(R_REFUSED, api, idx, -999, 0, 0, "no dest");
    size_t dmax = cap + (size_t)delta;
    const char *src = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    switch (api) {
    case A_SNPRINTF_RAW:
        rc = cplus_snprintf_s(dest, dmax, "%s", src); break;
    case A_SNPRINTF_CHK:
        rc = cplus_snprintf_s_checked(1, cap, dest, dmax, "%s", src); break;
    case A_SPRINTF_RAW:
        rc = cplus_sprintf_s(dest, dmax, "%s", src); break;
    case A_SPRINTF_CHK:
        rc = cplus_sprintf_s_checked(1, cap, dest, dmax, "%s", src); break;
    case A_FGETS_CHK: {
        FILE *f = tmpfile();
        if (f) { fputs("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n", f); rewind(f); }
        char *r = cplus_fgets_s_checked(dest, cap, (int)dmax, f);
        rc = r ? 1 : 0;
        if (f) fclose(f);
        break;
    }
    case A_FREAD_CHK: {
        FILE *f = tmpfile();
        if (f) { fwrite(src, 1, 64, f); rewind(f); }
        rc = (int)cplus_fread_s_checked(dest, cap, dmax, 1, dmax, f);
        if (f) fclose(f);
        break;
    }
    default: break;
    }
    uint32_t bad = guarded_check(&g);
    if (memcmp(m.trail, "\x5A\x5A\x5A\x5A", 4) != 0) bad |= CAN_MEMB;
    report(bad ? R_CORRUPT : R_REFUSED, api, idx, rc, 0, bad, "capacity");
}

/* ---------------- macro-guarded struct-member capacity ----------------
 * This is the include-order attack: in the io-first build the macro sees
 * __builtin_object_size(member, 0) == sizeof(whole struct), so dmax larger
 * than the member is accepted and the trailing member is overwritten. */
static void do_member_macro(int idx, int is_fgets)
{
    MemberBuf m;
    memset(&m, 0x5A, sizeof m);
    errno = 0;
    int rc = -999;
    size_t dmax = sizeof m.member + 1u + (size_t)(idx % 3);
    if (!is_fgets) {
        rc = snprintf_s(m.member, dmax, "%s",
                        "abcdefghijklmnopqrstuvwxyz0123456789");
    } else {
        FILE *f = tmpfile();
        if (f) { fputs("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\n", f); rewind(f); }
        char *r = fgets_s(m.member, (int)dmax, f);
        rc = r ? 1 : 0;
        if (f) fclose(f);
    }
    uint32_t bad = 0;
    if (memcmp(m.trail, "\x5A\x5A\x5A\x5A", 4) != 0) bad |= CAN_MEMB;
    report(bad ? R_CORRUPT : R_REFUSED,
           is_fgets ? A_FGETS_MACRO_MEMBER : A_SNPRINTF_MACRO_MEMBER,
           idx, rc, 0, bad, is_fgets ? "macro/member/fgets" : "macro/member/snprintf");
}

/* ---------------- stream attacks ---------------- */
static void do_stream(int idx)
{
    int ids[5] = { A_FCLOSE, A_FPRINTF_CLOSED, A_FWRITE_CLOSED,
                   A_FPRINTF_RAW, A_FWRITE_CHK };
    int api = ids[idx % 5];
    Guarded g; guarded_init(&g);
    errno = 0;
    int rc = -999;
    switch (api) {
    case A_FCLOSE: {
        FILE *f = fopen("/dev/null", "r");
        if (!f) report(R_REFUSED, api, idx, -999, errno, 0, "nofile");
        int r1 = cplus_fclose_s(f);
        int r2 = cplus_fclose_s(f);
        report((r1 == 0 && r2 == EOF) ? R_REFUSED : R_UNEXPECTED_OK,
               api, idx, r2, 0, 0, "double-close");
        break;
    }
    case A_FPRINTF_CLOSED: {
        FILE *f = fopen("/dev/null", "w");
        if (!f) report(R_REFUSED, api, idx, -999, errno, 0, "nofile");
        cplus_fclose_s(f);
        rc = cplus_fprintf_s(f, "hello %d", 42);
        break;
    }
    case A_FWRITE_CLOSED: {
        FILE *f = fopen("/dev/null", "w");
        if (!f) report(R_REFUSED, api, idx, -999, errno, 0, "nofile");
        cplus_fclose_s(f);
        rc = (int)cplus_fwrite_s_checked(g.buf, sizeof g.buf, sizeof g.buf,
                                         1, 4, f);
        break;
    }
    case A_FPRINTF_RAW:
        rc = cplus_fprintf_s((idx & 1) ? NULL : fopen("/dev/null", "w"),
                             "x %d", 1);
        break;
    default: {
        FILE *f = (idx & 1) ? NULL : fopen("/dev/null", "w");
        rc = (int)cplus_fwrite_s_checked(g.buf, sizeof g.buf, sizeof g.buf,
                                         1, 4, f);
        if (f) fclose(f);
        break;
    }
    }
    uint32_t bad = guarded_check(&g);
    report(bad ? R_CORRUPT : R_REFUSED, api, idx, rc, 0, bad, "stream");
}

/* ---------------- non-literal formats ---------------- */
static void do_nonlit(int idx)
{
    int ids[4] = { A_SNPRINTF_CHK, A_SPRINTF_CHK, A_SSCANF_CHK, A_PRINTF_RAW };
    int api = ids[idx % 4];
    Guarded g; guarded_init(&g);
    char dynamic[64];
    snprintf(dynamic, sizeof dynamic, "%s", "%1$h1$n %s");
    errno = 0;
    int rc = -999;
    switch (api) {
    case A_SNPRINTF_CHK:
        rc = cplus_snprintf_s_checked(0, sizeof g.buf, (char *)g.buf,
                                      sizeof g.buf, dynamic); break;
    case A_SPRINTF_CHK:
        rc = cplus_sprintf_s_checked(0, sizeof g.buf, (char *)g.buf,
                                     sizeof g.buf, dynamic); break;
    case A_SSCANF_CHK:
        rc = cplus_sscanf_s_checked(0, "abc", dynamic); break;
    case A_PRINTF_RAW:
        rc = cplus_printf_s((idx & 1) ? NULL : dynamic); break;
    default: break;
    }
    uint32_t bad = guarded_check(&g);
    report(bad ? R_CORRUPT : R_REFUSED, api, idx, rc, 0, bad, "nonliteral");
}

/* ---------------- wide width / precision stress ---------------- */
static void do_width(int idx)
{
    Guarded g; guarded_init(&g);
    char fmt[64];
    int width = 1 + (idx % 512);
    int prec  = (idx / 7) % 64;
    snprintf(fmt, sizeof fmt, "%%%d.%ds", width, prec);
    errno = 0;
    int rc = cplus_snprintf_s((char *)g.buf, sizeof g.buf, fmt,
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    uint32_t bad = guarded_check(&g);
    report(bad ? R_CORRUPT : R_REFUSED, A_SNPRINTF_RAW, idx, rc, 0, bad, fmt);
}

/* ---------------- child dispatcher ---------------- */
enum { F_PN = 0, F_CAP, F_SCAN, F_STREAM, F_NONLIT, F_WIDTH, F_MEMBER, NFAM };

static void child_case(int fam, int idx, int fd)
{
    g_result_fd = fd;
    cplus_runtime_set_violation_trap(0);   /* return -1 instead of _exit(70) */
    alarm(3);                              /* per-case watchdog */
    /* Keep hostile output off the terminal and avoid blocking on stdin. */
    if (freopen("/dev/null", "w", stdout) == NULL) { /* ignore */ }
    if (freopen("/dev/null", "r", stdin)  == NULL) { /* ignore */ }

    switch (fam) {
    case F_PN:      do_pn(idx); break;
    case F_CAP:     do_capacity(idx); break;
    case F_SCAN:    do_scan(idx); break;
    case F_STREAM:  do_stream(idx); break;
    case F_NONLIT:  do_nonlit(idx); break;
    case F_WIDTH:   do_width(idx); break;
    case F_MEMBER:  do_member_macro(idx, (idx / 20) & 1); break;
    default: report(R_REFUSED, -1, idx, 0, 0, 0, "unknown");
    }
    report(R_REFUSED, -1, idx, 0, 0, 0, "fellthrough");
}

/* ---------------- parent ---------------- */
typedef struct { long cases, crash, corrupt, unexpected, refused; } Tally;

static int fam_of(int i) {
    /* 55% %n, 15% capacity, 12% member macro, 8% scanf, 5% stream,
     * 3% non-literal, 2% width -- deterministic round-robin avoids clumping. */
    static const int seq[20] = {
        F_PN, F_CAP, F_PN, F_MEMBER, F_PN, F_SCAN, F_PN, F_CAP,
        F_PN, F_STREAM, F_PN, F_MEMBER, F_PN, F_SCAN, F_PN, F_CAP,
        F_PN, F_NONLIT, F_PN, F_WIDTH
    };
    return seq[i % 20];
}

int main(int argc, char **argv)
{
    long n = (argc > 1) ? strtol(argv[1], NULL, 10) : 3600L;
    if (n < 1) n = 3600L;
    const char *logpath = (argc > 2) ? argv[2] : NULL;
    FILE *log = logpath ? fopen(logpath, "w") : NULL;

    cplus_runtime_set_violation_trap(0);
    setvbuf(stdout, NULL, _IONBF, 0);
    struct rlimit core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &core);

    Tally t = { 0, 0, 0, 0, 0 };
    for (long i = 0; i < n; ++i) {
        int pipefd[2];
        if (pipe(pipefd) != 0) { t.crash++; continue; }
        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) { close(pipefd[0]); close(pipefd[1]); t.crash++; continue; }
        if (pid == 0) {
            close(pipefd[0]);
            child_case(fam_of((int)i), (int)i, pipefd[1]);
            _exit(0);
        }
        close(pipefd[1]);
        Result r;
        memset(&r, 0, sizeof r);
        ssize_t got = read(pipefd[0], &r, sizeof r);
        close(pipefd[0]);
        int st = 0;
        while (waitpid(pid, &st, 0) < 0) { /* EINTR retry */ }

        int cls;
        if (WIFSIGNALED(st)) cls = R_CRASH;
        else if (got != (ssize_t)sizeof r || r.magic != R_MAGIC) cls = R_CRASH;
        else cls = r.cls;

        t.cases++;
        switch (cls) {
        case R_CORRUPT:       t.corrupt++; break;
        case R_UNEXPECTED_OK: t.unexpected++; break;
        case R_CRASH:         t.crash++; break;
        default:              t.refused++; break;
        }
        if (log && cls != R_REFUSED) {
            fprintf(log, "%ld\t%s\t%s\tcls=%d\tvariant=%d\tret=%d\terr=%d\t"
                         "sentinel=%08x\tcanary=%x\tnote=%s\n",
                    i, fam_of((int)i) == F_PN ? "PN" :
                       fam_of((int)i) == F_CAP ? "CAP" :
                       fam_of((int)i) == F_MEMBER ? "MEMBER" :
                       fam_of((int)i) == F_SCAN ? "SCAN" :
                       fam_of((int)i) == F_STREAM ? "STREAM" :
                       fam_of((int)i) == F_NONLIT ? "NONLIT" : "WIDTH",
                    (r.magic == R_MAGIC) ? api_name(r.api) : "?",
                    cls, r.variant, r.ret, r.err, r.sentinel, r.canary,
                    (r.magic == R_MAGIC) ? r.note : "no-result");
        }
    }
    if (log) fclose(log);
    printf("RED20 format %ld %ld %ld %ld %ld\n",
           t.cases, t.crash, t.corrupt, t.unexpected, t.refused);
    return 0;
}

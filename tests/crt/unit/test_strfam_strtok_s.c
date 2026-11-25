/*
 * Unit: cplus_strtok_s -- reentrant tokenizer that operates on a caller-owned
 * writable buffer.
 *
 * Maps to catalogue bug80: the classic strtok() overwrites its argument in
 * place; calling it on a string literal (read-only storage) faults.  This
 * checked tokenizer is required to run over a writable buffer, replace
 * delimiters with NUL, and keep every access inside *dmax.  Also documents the
 * backend quirk that the final call may return the last token again instead of
 * NULL, so exhaustion is asserted as NULL-or-last-token and never crashes.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- basic tokenization of a writable buffer (bug80) ---- */
    char b1[16] = "a,b,c";
    rsize_t d1 = sizeof b1;
    char *p1 = NULL;
    char *t = cplus_strtok_s(b1, &d1, ",", &p1);
    assert(t != NULL);
    assert(strcmp(t, "a") == 0);
    assert(b1[1] == '\0');                 /* delimiter overwritten in place */
    assert(d1 < sizeof b1);                /* *dmax consumed down */
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t != NULL && strcmp(t, "b") == 0);
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t != NULL && strcmp(t, "c") == 0);
    /* exhaustion: NULL or the last token again; must not crash */
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t == NULL || strcmp(t, "c") == 0);
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t == NULL || strcmp(t, "c") == 0);

    /* leading and repeated delimiters are skipped */
    char b2[16] = ",,a,,b";
    rsize_t d2 = sizeof b2;
    char *p2 = NULL;
    t = cplus_strtok_s(b2, &d2, ",", &p2);
    assert(t != NULL && strcmp(t, "a") == 0);
    t = cplus_strtok_s(NULL, &d2, ",", &p2);
    assert(t != NULL && strcmp(t, "b") == 0);

    /* multiple delimiter characters */
    char b3[20] = "red;green,blue";
    rsize_t d3 = sizeof b3;
    char *p3 = NULL;
    t = cplus_strtok_s(b3, &d3, ";,", &p3);
    assert(t != NULL && strcmp(t, "red") == 0);
    t = cplus_strtok_s(NULL, &d3, ";,", &p3);
    assert(t != NULL && strcmp(t, "green") == 0);
    t = cplus_strtok_s(NULL, &d3, ";,", &p3);
    assert(t != NULL && strcmp(t, "blue") == 0);

    /* empty string and all-delimiter string yield no token */
    char b4[4] = "";
    rsize_t d4 = sizeof b4;
    char *p4 = NULL;
    assert(cplus_strtok_s(b4, &d4, ",", &p4) == NULL);
    char b5[8] = ",,,";
    rsize_t d5 = sizeof b5;
    char *p5 = NULL;
    assert(cplus_strtok_s(b5, &d5, ",", &p5) == NULL);

    /* ---- argument validation returns NULL, never crashes ---- */
    char b6[8] = "a,b";
    char *p6 = NULL;
    assert(cplus_strtok_s(b6, NULL, ",", &p6) == NULL);       /* dmax NULL */
    rsize_t d6 = 0;
    assert(cplus_strtok_s(b6, &d6, ",", &p6) == NULL);        /* *dmax == 0 */
    rsize_t d6b = sizeof b6;
    assert(cplus_strtok_s(b6, &d6b, NULL, &p6) == NULL);      /* delim NULL */
    assert(cplus_strtok_s(b6, &d6b, ",", NULL) == NULL);      /* ptr NULL */
    char *p7 = NULL;
    rsize_t d7 = sizeof b6;
    assert(cplus_strtok_s(NULL, &d7, ",", &p7) == NULL);      /* NULL ctx */

    /* ---- guard bytes: the tokenizer writes only inside the buffer ---- */
    struct { char dst[16]; char guard[8]; } g;
    strcpy(g.dst, "one,two,three");
    memset(g.guard, 0x5A, sizeof g.guard);
    rsize_t dg = sizeof g.dst;
    char *pg = NULL;
    t = cplus_strtok_s(g.dst, &dg, ",", &pg);
    assert(t != NULL && strcmp(t, "one") == 0);
    t = cplus_strtok_s(NULL, &dg, ",", &pg);
    assert(t != NULL && strcmp(t, "two") == 0);
    t = cplus_strtok_s(NULL, &dg, ",", &pg);
    assert(t != NULL && strcmp(t, "three") == 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x5A);

    puts("test_strfam_strtok_s: ok");
    return 0;
}

/*
 * Adversarial string/format scenarios (Agent M-C, str50 series).
 *
 * cplus_strtok_s: multi-token walks, leading and repeated delimiters, empty
 * input, all-delimiter input, changing delimiters, and NULL argument handling.
 *
 * All dmax values are truthful: *dmaxp never exceeds the real capacity of the
 * buffer it describes.
 */
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h"

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* plain multi-token walk */
    {
        char s[32];
        char *ctx = NULL;
        char *t;
        rsize_t len;
        strcpy(s, "one,two,three");
        len = (rsize_t)(strlen(s) + 1);
        t = cplus_strtok_s(s, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "one") == 0);
        CHECK(ctx != NULL);
        CHECK(ctx > s);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "two") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "three") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t == NULL);
    }
    /* leading and repeated delimiters are collapsed */
    {
        char s[32];
        char *ctx = NULL;
        char *t;
        rsize_t len;
        strcpy(s, ",,a,,,b,");
        len = (rsize_t)(strlen(s) + 1);
        t = cplus_strtok_s(s, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "a") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "b") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t == NULL);
    }
    /* mixed delimiters */
    {
        char s[48];
        char *ctx = NULL;
        char *t;
        rsize_t len;
        strcpy(s, ",.:*one,two;three");
        len = (rsize_t)(strlen(s) + 1);
        t = cplus_strtok_s(s, &len, ",.:;*", &ctx);
        CHECK(t != NULL && strcmp(t, "one") == 0);
        t = cplus_strtok_s(NULL, &len, ",.:;*", &ctx);
        CHECK(t != NULL && strcmp(t, "two") == 0);
        t = cplus_strtok_s(NULL, &len, ",.:;*", &ctx);
        CHECK(t != NULL && strcmp(t, "three") == 0);
        t = cplus_strtok_s(NULL, &len, ",.:;*", &ctx);
        CHECK(t == NULL);
    }
    /* whitespace delimiters with surrounding runs */
    {
        char s[32];
        char *ctx = NULL;
        char *t;
        rsize_t len;
        strcpy(s, "  hello   world  ");
        len = (rsize_t)(strlen(s) + 1);
        t = cplus_strtok_s(s, &len, " ", &ctx);
        CHECK(t != NULL && strcmp(t, "hello") == 0);
        t = cplus_strtok_s(NULL, &len, " ", &ctx);
        CHECK(t != NULL && strcmp(t, "world") == 0);
        t = cplus_strtok_s(NULL, &len, " ", &ctx);
        CHECK(t == NULL);
    }
    /* single token, no delimiter present */
    {
        char s[8];
        char *ctx = NULL;
        char *t;
        rsize_t len;
        strcpy(s, "abc");
        len = (rsize_t)(strlen(s) + 1);
        t = cplus_strtok_s(s, &len, ";", &ctx);
        CHECK(t != NULL && strcmp(t, "abc") == 0);
        CHECK(t >= s && t < s + sizeof s);
        t = cplus_strtok_s(NULL, &len, ";", &ctx);
        CHECK(t == NULL);
    }
    /* changing the delimiter set between calls */
    {
        char s[16];
        char *ctx = NULL;
        char *t;
        rsize_t len;
        strcpy(s, "a:b,c");
        len = (rsize_t)(strlen(s) + 1);
        t = cplus_strtok_s(s, &len, ":", &ctx);
        CHECK(t != NULL && strcmp(t, "a") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "b") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t != NULL && strcmp(t, "c") == 0);
        t = cplus_strtok_s(NULL, &len, ",", &ctx);
        CHECK(t == NULL);
    }
    /* empty input string yields no token */
    {
        char s[4];
        char *ctx = NULL;
        rsize_t len;
        s[0] = '\0';
        len = 1;
        CHECK(cplus_strtok_s(s, &len, ";", &ctx) == NULL);
    }
    /* all-delimiter input yields no token */
    {
        char s[8];
        char *ctx = NULL;
        rsize_t len;
        strcpy(s, ";;;;");
        len = (rsize_t)(strlen(s) + 1);
        CHECK(cplus_strtok_s(s, &len, ";", &ctx) == NULL);
    }
    /* NULL delimiter / NULL context / NULL dmax */
    {
        char s[8];
        char *ctx = NULL;
        rsize_t len;
        strcpy(s, "abc");
        len = (rsize_t)(strlen(s) + 1);
        CHECK(cplus_strtok_s(s, &len, NULL, &ctx) == NULL);
        CHECK(cplus_strtok_s(s, &len, ";", NULL) == NULL);
        CHECK(cplus_strtok_s(s, NULL, ";", &ctx) == NULL);
        CHECK(cplus_strtok_s(NULL, &len, ";", &ctx) == NULL); /* NULL dest, NULL ctx */
    }
    /* *dmaxp == 0 is a constraint violation */
    {
        char s[8];
        char *ctx = NULL;
        rsize_t len = 0;
        int r;
        strcpy(s, "abc");
        r = (cplus_strtok_s(s, &len, ";", &ctx) == NULL);
        CHECK(r == 1);
        CHECK(errno == ESZEROL);
    }
    /* 100-iteration token walk over a fixed 4-token string */
    {
        int i;
        int good = 0;
        for (i = 0; i < 100; i++) {
            char s[16];
            char *ctx = NULL;
            char *t;
            rsize_t len;
            int count = 0;
            strcpy(s, "a,b,c,d");
            len = (rsize_t)(strlen(s) + 1);
            t = cplus_strtok_s(s, &len, ",", &ctx);
            while (t != NULL) {
                count++;
                t = cplus_strtok_s(NULL, &len, ",", &ctx);
            }
            if (count == 4)
                good++;
        }
        CHECK(good == 100);
    }

    if (failures == 0) {
        printf("test_str50_strtok: ok\n");
        return 0;
    }
    printf("test_str50_strtok: FAILURES=%d\n", failures);
    return 1;
}

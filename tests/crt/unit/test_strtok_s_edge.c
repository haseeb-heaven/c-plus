#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* 1. basic tokenization */
    char b1[16] = "a,b,c";
    rsize_t d1 = sizeof(b1);
    char *p1 = NULL;
    char *t = cplus_strtok_s(b1, &d1, ",", &p1);
    assert(t && strcmp(t, "a") == 0);

    /* 2. continuation returns second token */
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t && strcmp(t, "b") == 0);

    /* 3. continuation returns third token */
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t && strcmp(t, "c") == 0);

    /* 4. exhaustion: implementation may return NULL or repeat last token; must not crash */
    t = cplus_strtok_s(NULL, &d1, ",", &p1);
    assert(t == NULL || strcmp(t, "c") == 0);

    /* 5. leading delimiters are skipped */
    char b2[16] = ",,a,,b";
    rsize_t d2 = sizeof(b2);
    char *p2 = NULL;
    t = cplus_strtok_s(b2, &d2, ",", &p2);
    assert(t && strcmp(t, "a") == 0);
    t = cplus_strtok_s(NULL, &d2, ",", &p2);
    assert(t && strcmp(t, "b") == 0);

    /* 6. multiple delimiters set */
    char b3[20] = "red;green,blue";
    rsize_t d3 = sizeof(b3);
    char *p3 = NULL;
    t = cplus_strtok_s(b3, &d3, ";,", &p3);
    assert(t && strcmp(t, "red") == 0);
    t = cplus_strtok_s(NULL, &d3, ";,", &p3);
    assert(t && strcmp(t, "green") == 0);
    t = cplus_strtok_s(NULL, &d3, ";,", &p3);
    assert(t && strcmp(t, "blue") == 0);

    /* 7. empty string returns NULL */
    char b4[4] = "";
    rsize_t d4 = sizeof(b4);
    char *p4 = NULL;
    assert(cplus_strtok_s(b4, &d4, ",", &p4) == NULL);

    /* 8. all-delimiter string returns NULL */
    char b5[8] = ",,,";
    rsize_t d5 = sizeof(b5);
    char *p5 = NULL;
    assert(cplus_strtok_s(b5, &d5, ",", &p5) == NULL);

    /* 9. NULL dmax pointer is rejected */
    char b6[8] = "a,b";
    char *p6 = NULL;
    assert(cplus_strtok_s(b6, NULL, ",", &p6) == NULL);

    /* 10. zero dmax is rejected */
    char b7[8] = "a,b";
    rsize_t d7 = 0;
    char *p7 = NULL;
    assert(cplus_strtok_s(b7, &d7, ",", &p7) == NULL);

    /* 11. NULL delim is rejected */
    char b8[8] = "a,b";
    rsize_t d8 = sizeof(b8);
    char *p8 = NULL;
    assert(cplus_strtok_s(b8, &d8, NULL, &p8) == NULL);

    /* 12. NULL ptr is rejected */
    char b9[8] = "a,b";
    rsize_t d9 = sizeof(b9);
    assert(cplus_strtok_s(b9, &d9, ",", NULL) == NULL);

    /* 13. continuation with NULL dest and NULL saved ptr returns NULL */
    char b10[8] = "a,b";
    rsize_t d10 = sizeof(b10);
    char *p10 = NULL;
    assert(cplus_strtok_s(NULL, &d10, ",", &p10) == NULL);
    (void)b10;

    /* 14. dmax is decremented as tokens are consumed */
    char b11[16] = "ab,cd";
    rsize_t d11 = sizeof(b11);
    char *p11 = NULL;
    t = cplus_strtok_s(b11, &d11, ",", &p11);
    assert(t && strcmp(t, "ab") == 0);
    assert(d11 < 16);

    return 0;
}

/*
 * Unit: guard-byte / sub-object containment across the checked string family.
 *
 * Maps to catalogue bug153 (a bounded copy overflows a struct sub-object into
 * its neighbour) and bug184 (append loop reusing the total buffer size).  Every
 * accepted operation must stay inside the declared bound; every rejected one
 * must leave the sub-object boundary and its guarded neighbour untouched.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
#include "safe_error.h"
#include "safe_log.h"

#ifndef ESNULLP
#define ESNULLP 400
#endif
#ifndef ESNOSPC
#define ESNOSPC 406
#endif

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    /* ---- bug153: a struct member must not overflow into its neighbour ---- */
    struct { char name[4]; unsigned guard; } rec;
    rec.guard = 0xA5A5A5A5u;
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "bob") == 0);
    assert(strcmp(rec.name, "bob") == 0);
    assert(rec.guard == 0xA5A5A5A5u);
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "abc") == 0);   /* exact */
    assert(strcmp(rec.name, "abc") == 0);
    assert(rec.guard == 0xA5A5A5A5u);
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "alice") == ESNOSPC);
    assert(rec.guard == 0xA5A5A5A5u);                                /* neighbour safe */
    assert(rec.name[0] == '\0');                                     /* cleared */
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "ok") == 0);    /* still usable */
    assert(rec.guard == 0xA5A5A5A5u);

    /* strncpy_s on the same sub-object */
    rec.guard = 0x5A5A5A5Au;
    assert(cplus_strncpy_s(rec.name, sizeof rec.name, "wxyz", 3) == 0);
    assert(strcmp(rec.name, "wxy") == 0);
    assert(rec.guard == 0x5A5A5A5Au);
    assert(cplus_strncpy_s(rec.name, sizeof rec.name, "wxyz", 10) == ESNOSPC);
    assert(rec.guard == 0x5A5A5A5Au);

    /* ---- trailing guard arrays, one per API ---- */
    struct { char buf[8]; char guard[8]; } g;

    /* strcpy_s */
    memset(g.guard, 0x11, sizeof g.guard);
    strcpy(g.buf, "a");
    assert(cplus_strcpy_s(g.buf, sizeof g.buf, "abcdefghij") == ESNOSPC);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x11);

    /* strcat_s */
    memset(g.guard, 0x22, sizeof g.guard);
    strcpy(g.buf, "ab");
    assert(cplus_strcat_s(g.buf, sizeof g.buf, "0123456789") == ESNOSPC);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x22);

    /* strncat_s */
    memset(g.guard, 0x33, sizeof g.guard);
    strcpy(g.buf, "ab");
    assert(cplus_strncat_s(g.buf, sizeof g.buf, "0123456789", 10) == ESNOSPC);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x33);

    /* bug184: append loop passing the total size every iteration */
    memset(g.guard, 0x44, sizeof g.guard);
    g.buf[0] = '\0';
    const char *parts[] = { "ab", "cd", "ef", "gh", "ij", "kl" };
    int rejected = 0;
    for (int i = 0; i < 6; ++i) {
        if (cplus_strncat_s(g.buf, sizeof g.buf, parts[i], sizeof g.buf) != 0) {
            rejected = 1;
            break;
        }
        assert(cplus_strnlen_s(g.buf, sizeof g.buf) < sizeof g.buf);
    }
    assert(rejected == 1);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x44);

    /* strcmp_s compares const inputs and writes nothing */
    memset(g.guard, 0x55, sizeof g.guard);
    strcpy(g.buf, "abc");
    int ind = 7;
    assert(cplus_strcmp_s(g.buf, sizeof g.buf, "abd", &ind) == 0);
    assert(ind < 0);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x55);

    /* strnlen_s reads only */
    memset(g.guard, 0x66, sizeof g.guard);
    strcpy(g.buf, "abc");
    assert(cplus_strnlen_s(g.buf, sizeof g.buf) == 3);
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x66);

    /* strerror_s */
    memset(g.guard, 0x77, sizeof g.guard);
    assert(cplus_strerror_s(g.buf, sizeof g.buf, EINVAL) == 0);
    assert(g.buf[0] != '\0');
    for (size_t i = 0; i < sizeof g.guard; ++i) assert(g.guard[i] == 0x77);

    /* strtok_s writes only inside the buffer */
    struct { char dst[16]; char guard[8]; } s;
    strcpy(s.dst, "aa,bb,cc");
    memset(s.guard, 0x70, sizeof s.guard);
    rsize_t dm = sizeof s.dst;
    char *ctx = NULL;
    char *tk = cplus_strtok_s(s.dst, &dm, ",", &ctx);
    assert(tk != NULL && strcmp(tk, "aa") == 0);
    tk = cplus_strtok_s(NULL, &dm, ",", &ctx);
    assert(tk != NULL && strcmp(tk, "bb") == 0);
    tk = cplus_strtok_s(NULL, &dm, ",", &ctx);
    assert(tk != NULL && strcmp(tk, "cc") == 0);
    for (size_t i = 0; i < sizeof s.guard; ++i) assert(s.guard[i] == 0x70);

    puts("test_strfam_guard_bytes: ok");
    return 0;
}

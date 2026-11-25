/* timeconv.cp -- epoch seconds -> fixed UTC date/time using arithmetic only.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/dev_tools/timeconv.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/dev_tools/timeconv
 *
 * Usage:
 *   timeconv <epoch-seconds>
 *
 * No wall clock, time_t, or libc date routine is used: the Gregorian date is
 * derived from Howard Hinnant's civil-from-days algorithm with integer
 * arithmetic only, so results are deterministic on every platform.
 *
 * Safety evidence (runtime refusal, not a formal proof):
 *   - negative or > 253402300799 (9999-12-31T23:59:59Z) is REFUSED
 *     ("out-of-range") rather than producing an ill-defined date;
 *   - a decimal string that overflows 64-bit accumulation is REFUSED
 *     ("overflow"); an empty or non-digit input is REFUSED ("invalid").
 *
 * The divisor 10 is a constant, so strict mode accepts the overflow guard.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

#define TC_MAX_EPOCH 253402300799ULL

/* Returns 1 on success, 0 on invalid, -1 on negative/out-of-range. */
static int tc_parse(const char *s, unsigned long long *out) {
    unsigned long long v = 0;
    size_t i = 0;
    if (s[0] == '\0') return 0;
    if (s[0] == '-') return -1;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned int d;
        if (s[i] < '0' || s[i] > '9') return 0;
        d = (unsigned int)(s[i] - '0');
        if (v > (18446744073709551615ULL - (unsigned long long)d) / 10ULL)
            return 0;
        v = v * 10ULL + (unsigned long long)d;
    }
    if (v > TC_MAX_EPOCH) return -1;
    *out = v;
    return 1;
}

/* Howard Hinnant's days-from-civil inverse (proleptic Gregorian). */
static void tc_civil_from_days(long long z, int *year, unsigned int *mon,
                               unsigned int *day) {
    long long era;
    unsigned int doe, yoe, doy, mp, dd, mm;
    long long yy;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned int)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = (long long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    dd = doy - (153 * mp + 2) / 5 + 1;
    mm = mp + (mp < 10 ? 3 : -9);
    *year = (int)(yy + (mm <= 2u));
    *mon = mm;
    *day = dd;
}

static char *tc_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int tc_copy(char **argv, size_t idx, char *buf, size_t cap) {
    string text = string(tc_arg(argv, idx));
    if (!text.valid()) { text.drop(); return 0; }
    if (text.size() + 1 > cap) { text.drop(); return 0; }
    if (text.substr(0, text.size(), buf, cap) != 0) { text.drop(); return 0; }
    text.drop();
    return 1;
}

int main(int argc, char **argv) {
    char s[40];
    unsigned long long epoch = 0;
    unsigned long long days;
    unsigned int sod, hh, mm, ss;
    int year = 0;
    unsigned int mon = 0;
    unsigned int day = 0;
    int pr = 0;

    if (argc != 2) {
        printf("TIMECONV REFUSED bad-args\n");
        return 1;
    }
    if (!tc_copy(argv, 1, s, sizeof(s))) {
        printf("TIMECONV REFUSED invalid\n");
        return 1;
    }
    pr = tc_parse(s, &epoch);
    if (pr == 0) {
        printf("TIMECONV REFUSED invalid\n");
        return 1;
    }
    if (pr < 0) {
        printf("TIMECONV REFUSED out-of-range\n");
        return 1;
    }

    days = epoch / 86400ULL;
    sod = (unsigned int)(epoch - days * 86400ULL);
    hh = sod / 3600u;
    mm = (sod - hh * 3600u) / 60u;
    ss = sod - hh * 3600u - mm * 60u;

    tc_civil_from_days((long long)days, &year, &mon, &day);

    printf("TIMECONV %llu %04d-%02u-%02uT%02u:%02u:%02uZ\n",
           epoch, year, mon, day, hh, mm, ss);
    return 0;
}

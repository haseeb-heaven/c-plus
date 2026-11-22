/* Platform-backed stdlib extras (C+ libc slice), batch 5. Delegate to the
 * installed system libc; logged per C+ convention. Covers the legacy
 * floating-point-to-string converters (ecvt/fcvt/gcvt) and the POSIX
 * drand48() pseudo-random family, both of which are ABI-tied to the host. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <stdlib.h>

char *cplus_ecvt(double value, int ndigit, int *restrict decpt, int *restrict sign) {
    cplus_libc_log("ecvt", "enter");
    return ecvt(value, ndigit, decpt, sign);
}
char *cplus_fcvt(double value, int ndigit, int *restrict decpt, int *restrict sign) {
    cplus_libc_log("fcvt", "enter");
    return fcvt(value, ndigit, decpt, sign);
}
char *cplus_gcvt(double value, int ndigit, char *restrict buf) {
    cplus_libc_log("gcvt", "enter");
    return gcvt(value, ndigit, buf);
}
double cplus_drand48(void) {
    cplus_libc_log("drand48", "enter");
    return drand48();
}
double cplus_erand48(unsigned short xsubi[3]) {
    cplus_libc_log("erand48", "enter");
    return erand48(xsubi);
}
unsigned short *cplus_seed48(unsigned short seed16v[3]) {
    cplus_libc_log("seed48", "enter");
    return seed48(seed16v);
}
long cplus_lrand48(void) {
    cplus_libc_log("lrand48", "enter");
    return lrand48();
}

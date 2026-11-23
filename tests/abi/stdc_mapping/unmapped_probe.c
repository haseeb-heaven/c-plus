/* Preprocessor/nm probe for names that are NOT remapped by safe_c.h.
 * The task brief lists fopen/fclose among "etc."; they are in fact NOT mapped
 * (only the opt-in fopen_s/fclose_s aliases exist in safe_io.h). This TU
 * proves the ordinary spellings still bind to host symbols. */
#include <stdio.h>
#include <string.h>

FILE *probe_fopen(const char *n, const char *m) { return fopen(n, m); }
int probe_fclose(FILE *f) { return fclose(f); }
size_t probe_strlen(const char *s) { return strlen(s); }
size_t probe_strnlen(const char *s, size_t n) { return strnlen(s, n); }

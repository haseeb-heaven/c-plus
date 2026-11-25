/* Format-string and %n write-primitive mistakes (bug IDs 455-464). */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/* ========================================================================== */
void bug455_percent_n_untrusted_pointer(void *untrusted_ptr) {
  // BUG 455: The format is a literal, but %n writes the number of converted
  // bytes through a caller-controlled pointer -- an arbitrary memory write.
  printf("value=%d%n", 7, (int *)untrusted_ptr);
  // SAFE FIX: never pass caller-controlled pointers as %n targets. Write into
  // a private local int and ignore it, or remove %n from the format entirely.
}

/* ========================================================================== */
void bug456_positional_n_write(void *slot) {
  // BUG 456: Positional specifiers such as %1$n are just as dangerous as plain
  // %n; the write target still comes from an untrusted source.
  printf("%1$n", (int *)slot);
  // SAFE FIX: use a validated local counter and a fixed format with no %n.
}

/* ========================================================================== */
void bug457_untrusted_format_from_config(const char *config_format) {
  // BUG 457: A format string loaded from configuration is effectively
  // attacker input; %n in it can overwrite memory. (The dummy argument keeps
  // the format-security check quiet, but the runtime bug is still present.)
  printf(config_format, 0);
  // SAFE FIX: printf("%s", config_format) -- treat the value as data, never
  // as a format.
}

/* ========================================================================== */
void bug458_fprintf_n_to_stream(FILE *out, const char *label, void *slot) {
  // BUG 458: Mixing user text with %n and an untrusted pointer turns a log
  // call into a write-what-where primitive on the destination stream.
  fprintf(out, "%s%n", label, (int *)slot);
  // SAFE FIX: fprintf(out, "%s", label); never let format output choose the
  // write address.
}

/* ========================================================================== */
void bug459_sprintf_n_to_buffer(char *dst, void *slot) {
  // BUG 459: sprintf with %n writes a byte count through slot even though the
  // visible destination is dst; the hidden side effect is easy to overlook.
  sprintf(dst, "x%n", (int *)slot);
  // SAFE FIX: build the string with snprintf and a bounded literal format
  // that contains no %n, and keep slot out of the call.
}

/* ========================================================================== */
void bug460_snprintf_zero_size_n_write(void *slot) {
  // BUG 460: A zero-size snprintf still evaluates %n and performs the write.
  snprintf(NULL, 0, "%n", (int *)slot);
  // SAFE FIX: do not rely on size 0 to suppress side effects; drop %n.
}

/* ========================================================================== */
void bug461_wide_swprintf_n_write(void *slot) {
  // BUG 461: The wide-character family has the same %n write primitive.
  wchar_t out[16];
  swprintf(out, 16, L"%n", (int *)slot);
  // SAFE FIX: use a literal wide format with no %n and a private counter.
}

/* ========================================================================== */
void bug462_half_write_n(void *slot) {
  // BUG 462: %hn writes a 16-bit count; a half-pointer overwrite is a common
  // exploit step when the target pointer is attacker supplied.
  printf("%hn", (short *)slot);
  // SAFE FIX: never expose %hn to untrusted pointers; validate the address
  // range or remove the conversion.
}

/* ========================================================================== */
void bug463_byte_write_n(void *slot) {
  // BUG 463: %hhn performs a single-byte write through the supplied pointer.
  printf("%hhn", (signed char *)slot);
  // SAFE FIX: avoid %hhn with untrusted addresses; use a local unsigned char
  // and copy only after explicit bounds checks.
}

/* ========================================================================== */
void bug464_two_n_writes(void *first, void *second) {
  // BUG 464: Two %n conversions in one call write two independent attacker
  // chosen locations, which is enough to build a limited write-what-where.
  printf("%n%n", (int *)first, (int *)second);
  // SAFE FIX: eliminate all %n conversions from externally influenced calls
  // and keep any counting in private state.
}

int main(void) { return 0; }

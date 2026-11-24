/*
 * safe_diag.c -- rich, colored, location-aware diagnostics for C+.
 *
 * Default output is byte-identical to the historical emitters. Rich output is
 * opt-in via CPLUS_DIAG=rich|plain or cplus_diag_set_mode(). No exported symbol
 * changes signature; this file only adds new ones.
 */
/* Required for isatty()/fileno() under glibc with -std=c99 -pedantic; without
 * this feature macro the Linux "Build safe CRT" CI step fails to compile. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "../safe_diag.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define CPLUS_DIAG_ISATTY(fd) _isatty(fd)
#define CPLUS_DIAG_FILENO(f) _fileno(f)
#else
#include <unistd.h>
#define CPLUS_DIAG_ISATTY(fd) isatty(fd)
#define CPLUS_DIAG_FILENO(f) fileno(f)
#endif

/* ------------------------------------------------------------ configuration */
static cplus_diag_mode g_mode = CPLUS_DIAG_LEGACY;
static cplus_diag_color g_color = CPLUS_DIAG_COLOR_AUTO;
static int g_env_initialized = 0;

static int parse_word(const char *value, const char *word) {
  size_t i = 0;
  if (!value || !word)
    return 0;
  while (value[i] == ' ' || value[i] == '\t')
    ++i;
  while (word[i]) {
    if (value[i] != word[i])
      return 0;
    ++i;
  }
  while (value[i] == ' ' || value[i] == '\t')
    ++i;
  return value[i] == '\0';
}

void cplus_diag_init_from_env(void) {
  const char *diag = getenv("CPLUS_DIAG");
  const char *color = getenv("CPLUS_COLOR");
  const char *no_color = getenv("NO_COLOR");

  g_env_initialized = 1;

  if (parse_word(diag, "rich"))
    g_mode = CPLUS_DIAG_RICH;
  else if (parse_word(diag, "plain"))
    g_mode = CPLUS_DIAG_PLAIN;
  else if (parse_word(diag, "legacy") || (diag && diag[0] == '\0'))
    g_mode = CPLUS_DIAG_LEGACY;
  /* Unrecognised CPLUS_DIAG leaves the programmatic/default mode intact. */

  if (parse_word(color, "always"))
    g_color = CPLUS_DIAG_COLOR_ALWAYS;
  else if (parse_word(color, "never"))
    g_color = CPLUS_DIAG_COLOR_NEVER;
  else if (parse_word(color, "auto") || (color && color[0] == '\0'))
    g_color = CPLUS_DIAG_COLOR_AUTO;

  /* NO_COLOR follows the de-facto standard: any non-empty value disables
   * color, even when CPLUS_COLOR=always was requested. */
  if (no_color && no_color[0] != '\0') {
    if (g_color == CPLUS_DIAG_COLOR_ALWAYS)
      g_color = CPLUS_DIAG_COLOR_NEVER;
  }
}

static void ensure_env(void) {
  if (!g_env_initialized)
    cplus_diag_init_from_env();
}

void cplus_diag_set_mode(cplus_diag_mode mode) {
  ensure_env();
  g_mode = mode;
}

cplus_diag_mode cplus_diag_get_mode(void) {
  ensure_env();
  return g_mode;
}

void cplus_diag_set_color(cplus_diag_color color) {
  ensure_env();
  g_color = color;
}

cplus_diag_color cplus_diag_get_color(void) {
  ensure_env();
  return g_color;
}

int cplus_diag_color_enabled(FILE *out) {
  const char *no_color;
  ensure_env();
  if (g_mode == CPLUS_DIAG_PLAIN)
    return 0;
  if (g_color == CPLUS_DIAG_COLOR_NEVER)
    return 0;
  no_color = getenv("NO_COLOR");
  if (no_color && no_color[0] != '\0')
    return 0;
  if (g_color == CPLUS_DIAG_COLOR_ALWAYS)
    return 1;
  if (!out)
    out = stderr;
  return CPLUS_DIAG_ISATTY(CPLUS_DIAG_FILENO(out)) ? 1 : 0;
}

/* -------------------------------------------------------------- code table */
typedef struct diag_code_info {
  int code;
  const char *name;
  const char *meaning;
  const char *hint;
} diag_code_info;

/* Codes emitted by the C+ runtime, SafeCRT adapters and vendored safeclib.
 * Every entry carries a name, a plain-language meaning and an actionable hint
 * so a rich diagnostic genuinely "explains" the error rather than just
 * decorating it. */
static const diag_code_info kCodes[] = {
    {0, "EOK", "The operation completed successfully.", "No action required."},
    {12, "ENOMEM", "Out of memory: the allocation could not be satisfied.",
     "Reduce the requested size, release unused blocks, or check the allocator "
     "limit."},
    {22, "EINVAL", "An argument was NULL, out of range, or otherwise invalid.",
     "Check every pointer and size argument against the API contract before "
     "calling."},
    {34, "ERANGE", "A value was outside the representable or allowed range.",
     "Use a smaller size/count or widen the destination buffer."},
    {70, "EX_SOFTWARE", "Controlled process failure after a runtime safety "
                        "violation.",
     "Inspect the preceding diagnostic; the offending operation was not "
     "performed."},
    {75, "EOVERFLOW", "A computation or conversion overflowed.",
     "Validate the operands and use a wider type or a checked multiply."},
    {95, "EUNAVAIL", "The requested feature is unavailable on this host.",
     "Check platform support and feature-test macros."},
    {400, "ESNULLP", "A required pointer argument was NULL.",
     "Pass a valid, non-NULL pointer; null is a constraint violation."},
    {401, "ESZEROL", "A required length was zero.",
     "Pass a positive length where the API requires one."},
    {403, "ESLEMAX", "A length or value exceeded RSIZE_MAX.",
     "Clamp the value to the documented RSIZE_MAX_{MEM,STR} limit."},
    {406, "ESNOSPC", "The destination has too little space for the result.",
     "Pass a correct dmax/bmax, or use a bounded format and a larger buffer."},
    {409, "ESNOTFND", "The requested item was not found.",
     "Check the key/index and verify the container is populated."},
    {101, "CPLUS_EXIT_BOUNDS", "An access went outside object bounds.",
     "Check the index and the object size; use at()/checked APIs."},
    {102, "CPLUS_EXIT_NULL_POINTER", "A NULL pointer was dereferenced.",
     "Validate the pointer before use; null is never a valid object."},
    {103, "CPLUS_EXIT_USE_AFTER_FREE", "A freed object was accessed.",
     "Stop using the object after release; use a checked handle/token."},
    {104, "CPLUS_EXIT_DOUBLE_FREE", "An object was released twice.",
     "Release each ownership exactly once; clear aliases after release."},
    {105, "CPLUS_EXIT_TYPE_CONFUSION", "An object was used as the wrong type.",
     "Check the handle's tag before casting or dispatching."},
    {106, "CPLUS_EXIT_OWNERSHIP", "An ownership or capability check failed.",
     "Only the owning scope may release or transfer this object."},
    {107, "CPLUS_EXIT_RESOURCE", "A system resource was exhausted or invalid.",
     "Check allocator limits, file descriptors and OS resource availability."},
    {108, "CPLUS_EXIT_UNSUPPORTED", "The operation is not supported here.",
     "Use a supported code path or check the host feature."},
    {109, "CPLUS_EXIT_IO", "An I/O operation failed.",
     "Check the path, permissions and device state; verify the handle is open."}};

static const diag_code_info *lookup(int code) {
  size_t i;
  for (i = 0; i < sizeof(kCodes) / sizeof(kCodes[0]); ++i)
    if (kCodes[i].code == code)
      return &kCodes[i];
  return NULL;
}

const char *cplus_diag_code_name(int code) {
  const diag_code_info *info = lookup(code);
  return info ? info->name : "UNKNOWN";
}

const char *cplus_diag_hint(int code) {
  const diag_code_info *info = lookup(code);
  return info ? info->hint : "Check the API contract for this error code.";
}

int cplus_diag_explain(int code, char *buf, size_t bufsz) {
  const diag_code_info *info = lookup(code);
  if (!buf || bufsz == 0)
    return info ? 1 : 0;
  if (info)
    snprintf(buf, bufsz, "%s (%d): %s Hint: %s", info->name, code,
             info->meaning, info->hint);
  else
    snprintf(buf, bufsz, "UNKNOWN (%d): unclassified error. Hint: check the "
                         "API contract for this code.",
             code);
  return info ? 1 : 0;
}

const char *cplus_diag_severity(const cplus_diag_record *rec) {
  if (!rec)
    return "notice";
  if (rec->kind == CPLUS_DIAG_VIOLATION)
    return "fatal";
  if (rec->kind == CPLUS_DIAG_RUNTIME)
    return "error";
  if (rec->level <= 1)
    return "error";
  if (rec->level == 2)
    return "warning";
  return "notice";
}

/* -------------------------------------------------------------- formatting */
static const char *safe_str(const char *s, const char *fallback) {
  return s ? s : fallback;
}

static void emit_legacy(const cplus_diag_record *rec, FILE *out) {
  if (!rec) {
    fputs("[CPLUS-RUNTIME] runtime error: (null)\n", out);
    return;
  }
  if (rec->kind == CPLUS_DIAG_VIOLATION) {
    fprintf(out, "[CPLUS-SAFE] %s: %s\n", safe_str(rec->api, "runtime"),
            safe_str(rec->detail, "memory safety violation"));
  } else if (rec->kind == CPLUS_DIAG_SAFECRT) {
    fprintf(out, "[SAFECRT][%d] %s: %s\n", rec->level,
            safe_str(rec->api, "(null)"), safe_str(rec->detail, "(null)"));
  } else {
    fprintf(out, "[CPLUS-RUNTIME] runtime error: %s\n",
            safe_str(rec->detail, "runtime error"));
  }
  fflush(out);
}

static void emit_rich(const cplus_diag_record *rec, FILE *out, int color) {
  const char *reset = color ? "\x1b[0m" : "";
  const char *bold_red = color ? "\x1b[1;91m" : "";
  const char *bold_white = color ? "\x1b[1;97m" : "";
  const char *cyan = color ? "\x1b[96m" : "";
  const char *dim = color ? "\x1b[2m" : "";
  const char *dim_cyan = color ? "\x1b[2;36m" : "";
  const char *severity = cplus_diag_severity(rec);
  /* A NULL record is a caller bug, but diagnostics must never crash: use a
   * safe all-zero record so the report still comes out (EDGE case). */
  cplus_diag_record empty;
  const diag_code_info *info;
  char explanation[512];
  if (!rec) {
    memset(&empty, 0, sizeof(empty));
    empty.kind = CPLUS_DIAG_RUNTIME;
    rec = &empty;
  }
  info = lookup(rec->code);
  cplus_diag_explain(rec->code, explanation, sizeof(explanation));

  /* Header: colored severity + code, symbolic name and API. */
  if (rec->code != 0) {
    fprintf(out, "%s%s[%d]%s %s%s%s %s%s%s\n", bold_red, severity, rec->code,
            reset, bold_white, info ? info->name : "UNKNOWN", reset,
            bold_white, safe_str(rec->api, "(unnamed)"), reset);
  } else {
    fprintf(out, "%s%s%s %s%s%s\n", bold_red, severity, reset, bold_white,
            safe_str(rec->api, "(unnamed)"), reset);
  }

  /* Source location: file:line in function. */
  if ((rec->file && rec->file[0]) || rec->line > 0 || rec->func) {
    fprintf(out, "%s  --> %s:%d in %s%s\n", cyan,
            safe_str(rec->file, "unknown"), rec->line > 0 ? rec->line : 0,
            safe_str(rec->func, "unknown"), reset);
  }

  /* The concrete detail of this occurrence. */
  fprintf(out, "%s   |%s\n", dim, reset);
  fprintf(out, "   | %s\n", safe_str(rec->detail, "(no detail)"));
  fprintf(out, "%s   |%s\n", dim, reset);

  /* Explanation + remediation hint. The first line carries the meaning;
   * the "Hint:" line is separated so it is easy to spot in a wall of logs. */
  fprintf(out, "%s   = %s%s\n", dim, explanation, reset);
  if (info)
    fprintf(out, "%s     Hint: %s%s\n", dim_cyan, info->hint, reset);

  fflush(out);
}

void cplus_diag_emit(const cplus_diag_record *rec, FILE *out) {
  cplus_diag_mode mode;
  if (!out)
    out = stderr;
  ensure_env();
  mode = g_mode;
  if (mode == CPLUS_DIAG_LEGACY) {
    emit_legacy(rec, out);
    return;
  }
  emit_rich(rec, out, cplus_diag_color_enabled(out));
}

/* ------------------------------------------------- location-aware wrappers */
/* Infer the most specific code from the violation detail so the rich mode
 * explains the actual failure class rather than a generic one. */
static int infer_violation_code(const char *detail) {
  const char *d = detail ? detail : "";
  if (strstr(d, "double") || strstr(d, "released twice"))
    return 104; /* CPLUS_EXIT_DOUBLE_FREE */
  if (strstr(d, "use-after-free") || strstr(d, "already freed") ||
      strstr(d, "freed"))
    return 103; /* CPLUS_EXIT_USE_AFTER_FREE */
  if (strstr(d, "closed") || strstr(d, "stream") || strstr(d, "I/O"))
    return 109; /* CPLUS_EXIT_IO */
  if (strstr(d, "null") || strstr(d, "NULL"))
    return 102; /* CPLUS_EXIT_NULL_POINTER */
  if (strstr(d, "overflow") || strstr(d, "capacity") || strstr(d, "bound") ||
      strstr(d, "exceeds") || strstr(d, "over-read") || strstr(d, "overwrite"))
    return 101; /* CPLUS_EXIT_BOUNDS */
  if (strstr(d, "generation") || strstr(d, "stale") || strstr(d, "ownership"))
    return 106; /* CPLUS_EXIT_OWNERSHIP */
  return 103; /* conservative default */
}

void cplus_diag_violation_at(const char *api, const char *detail,
                             const char *file, int line, const char *func) {
  cplus_diag_record rec;
  rec.kind = CPLUS_DIAG_VIOLATION;
  rec.level = 1;
  rec.code = infer_violation_code(detail);
  rec.api = api;
  rec.detail = detail;
  rec.file = file;
  rec.line = line;
  rec.func = func;
  cplus_diag_emit(&rec, stderr);
}

void cplus_diag_report_at(const char *message, const char *file, int line,
                          const char *func) {
  cplus_diag_record rec;
  rec.kind = CPLUS_DIAG_RUNTIME;
  rec.level = 1;
  rec.code = 70; /* controlled failure */
  rec.api = "cplus_runtime_report";
  rec.detail = message;
  rec.file = file;
  rec.line = line;
  rec.func = func;
  cplus_diag_emit(&rec, stderr);
}

void cplus_diag_log_at(int level, const char *api, const char *msg,
                       const char *file, int line, const char *func) {
  cplus_diag_record rec;
  rec.kind = CPLUS_DIAG_SAFECRT;
  rec.level = level;
  rec.code = (level <= 1) ? 22 : 0; /* EINVAL for error-level logs */
  rec.api = api;
  rec.detail = msg;
  rec.file = file;
  rec.line = line;
  rec.func = func;
  cplus_diag_emit(&rec, stderr);
}

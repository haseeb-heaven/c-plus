#ifndef CPLUS_SAFE_DIAG_H
#define CPLUS_SAFE_DIAG_H
/*
 * C+ rich diagnostics -- additive, opt-in error reporting.
 *
 * WHY THIS EXISTS
 * ---------------
 * The C+ runtime and SafeCRT emitters historically print one-line, plain-text
 * messages:
 *
 *   [CPLUS-SAFE] cplus_memcpy_s: source over-read prevented
 *   [SAFECRT][1] strcpy_s: null pointer
 *   [CPLUS-RUNTIME] runtime error: undefined behavior detected
 *
 * That format is stable because hundreds of tests and downstream consumers
 * match it byte-for-byte. This module therefore NEVER changes the default
 * output. It adds a richer mode that carries:
 *
 *   - a colored severity/code header (ANSI, with isatty/NO_COLOR detection),
 *   - the exact source file, line and function that raised the error,
 *   - a plain-language explanation of the error code and a remediation hint.
 *
 * Rich mode is selected with `CPLUS_DIAG=rich` (or `plain`, which keeps the
 * detail but drops color). `CPLUS_COLOR=always|never|auto` and `NO_COLOR`
 * control color independently. `cplus_diag_set_mode()` / `_set_color()` give
 * programmatic control for embedders and tests.
 *
 * This header is plain C99, header-additive, and introduces no ABI change:
 * all existing symbols keep their signatures and default behaviour.
 */
#include <stddef.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Output mode. LEGACY is the default and is byte-identical to the historical
 * one-line format. RICH adds color + location + explanation; PLAIN is RICH
 * without escape sequences (useful for log files and CI). */
typedef enum cplus_diag_mode {
  CPLUS_DIAG_LEGACY = 0,
  CPLUS_DIAG_RICH = 1,
  CPLUS_DIAG_PLAIN = 2
} cplus_diag_mode;

/* Color policy. AUTO enables color only when the stream is a terminal and
 * NO_COLOR is unset; ALWAYS/NEVER override the terminal check. */
typedef enum cplus_diag_color {
  CPLUS_DIAG_COLOR_AUTO = 0,
  CPLUS_DIAG_COLOR_NEVER = 1,
  CPLUS_DIAG_COLOR_ALWAYS = 2
} cplus_diag_color;

/* Which historical emitter a record corresponds to. This determines the
 * legacy prefix as well as the rich severity label. */
typedef enum cplus_diag_kind {
  CPLUS_DIAG_VIOLATION = 0, /* [CPLUS-SAFE] ...   : memory-safety violation */
  CPLUS_DIAG_SAFECRT = 1,   /* [SAFECRT][level] ...: checked-API refusal    */
  CPLUS_DIAG_RUNTIME = 2    /* [CPLUS-RUNTIME] ... : runtime error / UB     */
} cplus_diag_kind;

typedef struct cplus_diag_record {
  cplus_diag_kind kind;
  int level;        /* cplus_safe_log_level, only used for SAFECRT records */
  int code;         /* error code (CPLUS_EXIT_*, errno_t, ES*); 0 = none    */
  const char *api;  /* API/function name; may be NULL                      */
  const char *detail; /* human detail; may be NULL                         */
  const char *file; /* source file; may be NULL                            */
  int line;         /* source line; <= 0 means "unknown"                   */
  const char *func; /* source function; may be NULL                        */
} cplus_diag_record;

/* ------------------------------------------------------------------ config */
/* Read CPLUS_DIAG, CPLUS_COLOR and NO_COLOR once. Safe to call repeatedly;
 * the last call wins. Called lazily by the emitters on first use. */
void cplus_diag_init_from_env(void);

void cplus_diag_set_mode(cplus_diag_mode mode);
cplus_diag_mode cplus_diag_get_mode(void);

void cplus_diag_set_color(cplus_diag_color color);
cplus_diag_color cplus_diag_get_color(void);

/* Effective decision, accounting for the stream, NO_COLOR and the policy. */
int cplus_diag_color_enabled(FILE *out);

/* ------------------------------------------------------------- explanation */
/* Returns a short symbolic name ("ESNOSPC", "CPLUS_EXIT_BOUNDS", ...) or
 * "UNKNOWN". Never returns NULL. */
const char *cplus_diag_code_name(int code);

/* Whole, human-readable explanation including name, meaning and a hint.
 * Always NUL-terminated; returns 1 when the code is known, 0 otherwise. */
int cplus_diag_explain(int code, char *buf, size_t bufsz);

/* Remediation hint only. Never returns NULL; may be "". */
const char *cplus_diag_hint(int code);

/* Severity word for rich output ("fatal", "error", "refusal", "notice"). */
const char *cplus_diag_severity(const cplus_diag_record *rec);

/* ------------------------------------------------------------------ emit */
/* Format one record into `out`. Honours the current mode and color policy. */
void cplus_diag_emit(const cplus_diag_record *rec, FILE *out);

/* Location-capturing entry points. The historical entry points in
 * safe_log.h / safe_runtime.h forward here with an unknown location so the
 * exported ABI symbols stay intact; in-tree call sites use the *_here macros
 * so the real file/line/function is captured. */
void cplus_diag_violation_at(const char *api, const char *detail,
                             const char *file, int line, const char *func);
void cplus_diag_report_at(const char *message,
                          const char *file, int line, const char *func);
void cplus_diag_log_at(int level, const char *api, const char *msg,
                       const char *file, int line, const char *func);

/* Expand to the current source location. Use at the call site. */
#define CPLUS_DIAG_LOC __FILE__, __LINE__, __func__

#define cplus_diag_violation_here(api, detail)                                 \
  cplus_diag_violation_at((api), (detail), CPLUS_DIAG_LOC)
#define cplus_diag_report_here(msg)                                            \
  cplus_diag_report_at((msg), CPLUS_DIAG_LOC)
#define cplus_diag_log_here(level, api, msg)                                   \
  cplus_diag_log_at((level), (api), (msg), CPLUS_DIAG_LOC)

#if defined(__cplusplus)
}
#endif

#endif /* CPLUS_SAFE_DIAG_H */

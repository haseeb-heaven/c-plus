/* Minimal local sqlite3.h stub for tests/header_compat only.
 * This is NOT the real sqlite3 header: it declares just enough symbols to
 * exercise the rewriter against a third-party-style #include. */
#ifndef CPLUS_TEST_SQLITE3_STUB_H
#define CPLUS_TEST_SQLITE3_STUB_H

typedef struct sqlite3 sqlite3;
#define SQLITE_OK 0

int sqlite3_open(const char *filename, sqlite3 **db);
int sqlite3_close(sqlite3 *db);

#endif /* CPLUS_TEST_SQLITE3_STUB_H */

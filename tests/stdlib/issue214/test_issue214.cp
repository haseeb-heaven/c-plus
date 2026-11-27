/* Issue 214 regression: sqldb_load() must bound decoded name writes.
 *
 * The persisted format stores a name as hex.  A row with 64+ hex characters
 * used to fill all 32 bytes of `name[SQLDB_NAME_LEN]` and then write the NUL
 * at name[32], one byte past the stack array (caught by UBSan at runtime).
 * This test loads exactly such rows, checks the stored names are bounded, and
 * confirms a save/load round trip is unchanged.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/sqldb.hp"

#define ROWS_PATH "build/stdlib-tests/issue214_rows.txt"
#define SAVE_PATH "build/stdlib-tests/issue214_saved.txt"

static int failures = 0;

static void check(const char *label, int ok) {
    print("issue214", label, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static void write_hex_row(FILE *f, int id, int age, const char *bytes,
                          size_t n) {
    size_t i;
    fprintf(f, "row %d %d ", id, age);
    for (i = 0u; i < n; ++i)
        fprintf(f, "%02x", (unsigned char)bytes[i]);
    fprintf(f, "\n");
}

static void check_row(const sqldb *db, int id, const char *want, int age) {
    int found = 0;
    const sql_row *r = sqldb_select((sqldb *)db, id, &found);
    check("found", found == 1 && r != (const sql_row *)0);
    if (r != (const sql_row *)0)
        check("row", r->id == id && r->age == age &&
                     strcmp(r->name, want) == 0);
}

int main(void) {
    static const char name32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"; /* 32 */
    static const char name36[] = "abcdefghijklmnopqrstuvwxyz0123456789"; /* 36 */
    sqldb db;
    sqldb reloaded;
    FILE *f;

    f = fopen(ROWS_PATH, "w");
    if (f == (FILE *)0)
        return 1;
    write_hex_row(f, 1, 20, name32, 32u); /* 64 hex chars: the overflow case */
    write_hex_row(f, 2, 21, name36, 36u); /* longer than the decoder holds */
    fprintf(f, "row 3 22 616263\n");       /* "abc" */
    fprintf(f, "row 4 23 41zz42\n");       /* invalid pair stops the decoder */
    fprintf(f, "row 5 24\n");              /* no name token: ignored */
    fclose(f);

    sqldb_init(&db);
    check("load", sqldb_load(&db, ROWS_PATH) == 0);
    check("count", sqldb_count(&db) == 4);
    check_row(&db, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234", 20);
    check_row(&db, 2, "abcdefghijklmnopqrstuvwxyz01234", 21);
    check_row(&db, 3, "abc", 22);
    check_row(&db, 4, "A", 23);

    check("save", sqldb_save(&db, SAVE_PATH) == 0);
    sqldb_init(&reloaded);
    check("reload", sqldb_load(&reloaded, SAVE_PATH) == 0);
    check("reload-count", sqldb_count(&reloaded) == 4);
    check_row(&reloaded, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234", 20);
    check_row(&reloaded, 2, "abcdefghijklmnopqrstuvwxyz01234", 21);
    check_row(&reloaded, 3, "abc", 22);
    check_row(&reloaded, 4, "A", 23);

    print("issue214-summary", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

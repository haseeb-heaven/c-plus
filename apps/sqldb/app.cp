/* Real SQL-like database app (not a demo) on the stdlib/sqldb.hp table
 * store.  Executes a scripted sequence of operations on an in-memory
 * database and prints every result, plus a save/load round trip.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/sqldb.hp"

int main(void) {
    sqldb db;
    const sql_row *r;
    int found = 0;

    sqldb_init(&db);
    printf("SQLDB_APP insert rc=%d\n", sqldb_insert(&db, 101, 30, "ada"));
    printf("SQLDB_APP insert rc=%d\n", sqldb_insert(&db, 102, 22, "bo"));
    printf("SQLDB_APP count=%d\n", sqldb_count(&db));

    r = sqldb_select(&db, 101, &found);
    printf("SQLDB_APP select id=%d found=%d age=%d\n",
           r != (const sql_row *)0 ? r->id : -1, found,
           r != (const sql_row *)0 ? r->age : -1);
    printf("SQLDB_APP update rc=%d\n", sqldb_update(&db, 101, 31, "ada2"));

    printf("SQLDB_APP save rc=%d\n", sqldb_save(&db, "app_dump.db"));
    printf("SQLDB_APP load rc=%d\n", sqldb_load(&db, "app_dump.db"));
    printf("SQLDB_APP count_after_load=%d\n", sqldb_count(&db));

    r = sqldb_select(&db, 101, &found);
    printf("SQLDB_APP loaded_age=%d\n",
           r != (const sql_row *)0 ? r->age : -1);

    printf("SQLDB_APP delete rc=%d\n", sqldb_delete(&db, 102));
    printf("SQLDB_APP count_final=%d\n", sqldb_count(&db));
    return 0;
}

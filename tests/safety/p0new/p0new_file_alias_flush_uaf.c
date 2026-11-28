// scenario: p0new_file_alias_flush_uaf
// EXPECT: reject
// DIAG: use of closed FILE*
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_file_alias_flush_uaf(void){
    FILE *primary = fopen("p0new.txt", "a");
    if (!primary) return;
    FILE *secondary = primary;
    fclose(primary);
    fflush(secondary);
}

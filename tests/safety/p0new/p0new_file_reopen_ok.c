// scenario: p0new_file_reopen_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void p0new_file_reopen_ok(void){
    FILE *f = fopen("p0new.txt", "w");
    if (!f) return;
    fclose(f);
    f = fopen("p0new.txt", "r");
    if (!f) return;
    fclose(f);
}

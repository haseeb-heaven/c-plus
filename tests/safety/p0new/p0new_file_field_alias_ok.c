// scenario: p0new_file_field_alias_ok
// EXPECT: accept
// DIAG: 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct p0new_io { FILE *stream; };
void p0new_file_field_alias_ok(void){
    struct p0new_io h;
    h.stream = fopen("p0new.log", "w");
    if (!h.stream) return;
    FILE *mirror = h.stream;
    fputs("ok\n", mirror);
    fclose(h.stream);
}

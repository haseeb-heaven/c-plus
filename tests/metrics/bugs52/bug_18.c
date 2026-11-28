#include <stdlib.h>
struct String { char* data; };
struct User { struct String name; };
int main(void) { struct User* u = (struct User*)malloc(sizeof(struct User)); u->name.data = (char*)malloc(32); free(u); return 0; }

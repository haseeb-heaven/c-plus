#include <stdio.h>
int write_log(int abort_write) { FILE* f = fopen("app.log", "w"); if (!f) return -1; if (abort_write) { return -1; } fclose(f); return 0; }
int main(void) { return write_log(1); }

#include <stdio.h>
void attach_buffer(FILE* f) { char local_buf[BUFSIZ]; setbuf(f, local_buf); }
int main(void) { attach_buffer(stdout); printf("Writing into destroyed stack memory\n"); return 0; }

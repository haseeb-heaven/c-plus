#include <stdio.h>
void read_line(char*buf){fgets(buf,sizeof(buf),stdin);}
int main(void){char line[64];read_line(line);return 0;}

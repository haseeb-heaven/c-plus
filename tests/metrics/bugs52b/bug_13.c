#include <stdio.h>
struct Packet { char type; int payload; };
int main(void){ struct Packet pkt; pkt.type=1; pkt.payload=42; FILE*f=fopen("out.bin","wb"); if(f){fwrite(&pkt,sizeof(pkt),1,f);fclose(f);} return 0; }

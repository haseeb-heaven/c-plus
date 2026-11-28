#include <signal.h>
#include <stdlib.h>
void* g_buf=NULL;
void handle_sig(int sig){(void)sig; free(g_buf); g_buf=malloc(64);}
int main(void){ signal(SIGINT,handle_sig); for(int i=0;i<1000;i++){g_buf=malloc(64);free(g_buf);} return 0; }

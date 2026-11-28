#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
struct Session { int authenticated; char *session_token; };
int main(void){struct Session s={.authenticated=1,.session_token=malloc(64)}; if(!s.session_token)return 1; int op=0; printf("opcodes: "); while(scanf("%d",&op)==1&&op!=0){if(op==1)s.authenticated=0; else if(op==3) free(s.session_token);} return 0;}

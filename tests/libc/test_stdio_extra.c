#include <assert.h>
#include <stdio.h>
#include <string.h>
int cplus_putchar(int); int cplus_getchar(void); int cplus_puts(const char*); void cplus_perror(const char*);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
int main(void){
    for(int i=0;i<10;i++) assert(cplus_puts("ok")>=0);
    for(int i=0;i<10;i++) assert(cplus_putchar('\n')=='\n');
    /* getchar from stdin redirected */
    FILE*w=fopen("/tmp/cplus_getchar.tmp","wb"); fputs("A",w); fclose(w);
    for(int i=0;i<10;i++){
        FILE*old=stdin; stdin=fopen("/tmp/cplus_getchar.tmp","rb");
        int c=cplus_getchar(); if(stdin) fclose(stdin); stdin=old; assert(c=='A'||c==EOF);
    }
    for(int i=0;i<10;i++) cplus_perror("cplus");
    remove("/tmp/cplus_getchar.tmp");
    return 0;
}

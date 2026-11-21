/* musl-derived string functions integrated into the C+ libc slice. */
#include <assert.h>
#include <string.h>
#include <stddef.h>

void *cplus_memccpy(void*,const void*,int,size_t);
char *cplus_stpcpy(char*,const char*);
char *cplus_stpncpy(char*,const char*,size_t);
char *cplus_strtok_r(char*,const char*,char**);
char *cplus_strsep(char**,const char*);

int main(void){
    char d[32];
    /* memccpy: stops after copying c */
    for (int i=0;i<10;i++){ memset(d,0,sizeof d); void *r=cplus_memccpy(d,"abcdef",'c',6); assert(r==d+3); assert(memcmp(d,"abc",3)==0); }
    assert(cplus_memccpy(d,"abcdef",'z',6)==NULL);

    /* stpcpy: returns pointer to terminating NUL */
    for (int i=0;i<10;i++){ char b[16]; char *e=cplus_stpcpy(b,"hello"); assert(e==b+5 && strcmp(b,"hello")==0); }

    /* stpncpy: pads with NUL, returns pointer into dest */
    for (int i=0;i<10;i++){ char b[8]; memset(b,'X',sizeof b); char *e=cplus_stpncpy(b,"abc",8); assert(e==b+3); assert(strcmp(b,"abc")==0); }
    { char b[4]; memset(b,'X',4); char *e=cplus_stpncpy(b,"abcdef",4); assert(e==b+4); assert(memcmp(b,"abcd",4)==0); }

    /* strtok_r: tokenize with saveptr */
    for (int i=0;i<10;i++){ char s[]="a,b,c"; char *sp=NULL; char *t=cplus_strtok_r(s,",",&sp);
        assert(t && strcmp(t,"a")==0); t=cplus_strtok_r(NULL,",",&sp); assert(t && strcmp(t,"b")==0);
        t=cplus_strtok_r(NULL,",",&sp); assert(t && strcmp(t,"c")==0); assert(cplus_strtok_r(NULL,",",&sp)==NULL); }
    { char s[]=""; char *sp=NULL; assert(cplus_strtok_r(s,",",&sp)==NULL); }

    /* strsep: split on separator, empty fields preserved */
    for (int i=0;i<10;i++){ char s[]="one::three"; char *p=s;
        char *f1=cplus_strsep(&p,":"); assert(f1 && strcmp(f1,"one")==0);
        char *f2=cplus_strsep(&p,":"); assert(f2 && strcmp(f2,"")==0);
        char *f3=cplus_strsep(&p,":"); assert(f3 && strcmp(f3,"three")==0);
        assert(cplus_strsep(&p,":")==NULL); }
    { char *p=NULL; assert(cplus_strsep(&p,":")==NULL); }

    return 0;
}

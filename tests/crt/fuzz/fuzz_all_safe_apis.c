#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_log.h"

static unsigned rnd_state=1u;
static unsigned rnd(void){ rnd_state=rnd_state*1664525u+1013904223u; return rnd_state; }
static int vsn(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsnprintf_s(b,n,f,ap);va_end(ap);return r;}

int main(void){
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    for(int i=0;i<2000;i++){
        unsigned char s[64],d[64]; for(int j=0;j<64;j++) s[j]=(unsigned char)rnd();
        size_t dn = rnd()%65, sn=rnd()%65;
        (void)cplus_memcpy_s(d,dn,s,sn<dn?sn:dn);
        (void)cplus_memmove_s(d,dn,d, sn<dn?sn:dn);
        (void)cplus_memset_s(d,dn,(int)(rnd()&0xff),dn);
        int diff=0; (void)cplus_memcmp_s(d,dn,s,dn,&diff);
        (void)cplus_memzero_s(d,dn);

        char sd[32], ss[32]; size_t sl=rnd()%32; for(size_t j=0;j<31;j++) ss[j]=(char)('a'+(rnd()%26)); ss[sl<31?sl:31]=0;
        (void)cplus_strcpy_s(sd,rnd()%33,ss);
        (void)cplus_strncpy_s(sd,rnd()%33,ss,rnd()%40);
        strcpy(sd,"a"); (void)cplus_strcat_s(sd,rnd()%33,ss);
        strcpy(sd,"a"); (void)cplus_strncat_s(sd,rnd()%33,ss,rnd()%40);
        int ind=0; (void)cplus_strcmp_s(sd,32,ss,&ind);
        (void)cplus_strnlen_s(ss,rnd()%33);

        size_t sz=rnd()%4096; void *p=malloc_s(sz); if(p){ ((char*)p)[0]=1; size_t nz=rnd()%8192; void *np=realloc_s(p,nz); if(np) p=np; free_s(p);} 
        void *c=calloc_s(rnd()%64,rnd()%64); if(c) free_s(c);

        char pth[64]; snprintf(pth,64,"/tmp/cplus_fuzz_%d.tmp",i%16);
        FILE*f=cplus_fopen_s(pth,(i%2)?"wb+":"wb"); if(f){
            char buf[32]="data"; (void)cplus_fwrite_s(buf,32,1,rnd()%5,f);
            rewind(f); char rb[32]; (void)cplus_fread_s(rb,32,1,rnd()%5,f);
            (void)cplus_fprintf_s(f,"%d",i);
            cplus_fclose_s(f);
            remove(pth);
        }
        char fb[32]; (void)cplus_snprintf_s(fb,rnd()%33,"%s %d",ss,i);
        (void)cplus_sprintf_s(fb,sizeof(fb),"%d",i);
        (void)vsn(fb,rnd()%33,"%d",i);
        { va_list ap; /* vsprintf_s smoke via helper */ (void)ap; }
    }
    puts("fuzz_all_safe_apis: ok");
    return 0;
}

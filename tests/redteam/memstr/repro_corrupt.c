/* Minimal standalone reproducer for the primary REDTEAM finding:
 * cplus_sc_memcpy_s / cplus_memmove_s clear dmax bytes on a self-detected
 * constraint violation even when dmax > the real destination capacity.
 *
 * Build: clang -O0 -g -std=c99 -I<stdlib/safe> -I<safeclib/include> \
 *        -I<intel-safestringlib/include> repro_corrupt.c \
 *        build/safe/libcplus_safecrt.a build/libcplus_core.a -pthread -o repro_corrupt
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/resource.h>
typedef int errno_t; typedef size_t rsize_t;
extern errno_t cplus_sc_memcpy_s(void*, rsize_t, const void*, rsize_t);
extern errno_t cplus_memmove_s(void*, rsize_t, const void*, rsize_t);
extern void cplus_safe_set_constraint_mode(int);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char*,void*,int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char*,void*,int));
static void noop(const char*m,void*p,int e){(void)m;(void)p;(void)e;}
#define CAN 0xC3u
int main(int argc, char**argv){
    unsigned char buf[600]; int rc; size_t i; rsize_t dm;
    struct rlimit rl={0,0}; setrlimit(RLIMIT_CORE,&rl);
    cplus_safe_set_constraint_mode(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop);
    dm = (argc>1)?(rsize_t)strtoull(argv[1],0,10):5740;
    memset(buf,CAN,sizeof buf);
    /* null src on memcpy (also triggers wild clear); on memmove use null src too */
    if (argc>2 && strcmp(argv[2],"memmove")==0)
        rc = cplus_memmove_s(buf, dm, NULL, 3);
    else
        rc = cplus_sc_memcpy_s(buf, dm, NULL, 3);
    printf("api=%s dmax=%zu rc=%d\n",(argc>2?argv[2]:"memcpy"), dm, rc);
    for(i=0;i<600;i+=1) if(buf[i]!=CAN){printf("first corrupted byte at offset %zu (value %02x)\n",i,buf[i]);break;}
    /* count corrupted bytes */
    {size_t n=0; for(i=0;i<600;i++) if(buf[i]!=CAN) n++; printf("corrupted bytes (within 600B window)=%zu\n",n);}
    return 0;
}

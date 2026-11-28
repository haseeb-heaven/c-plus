/* Exact standalone reproducer for case 679 (strncpy_s, dm=cap+1, overlap). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/resource.h>
typedef int errno_t; typedef size_t rsize_t;
extern errno_t cplus_sc_strncpy_s(char *, rsize_t, const char *, rsize_t);
extern void cplus_safe_set_constraint_mode(int);
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char*,void*,int));
static void noop(const char*m,void*p,int e){(void)m;(void)p;(void)e;}
#define CAP 231
#define CAN 0xC3u
int main(void){
    unsigned char buf[320]; int rc; size_t i; int off = 1 + (11468 % (CAP > 8 ? 8 : CAP-1));
    struct rlimit rl={0,0}; setrlimit(RLIMIT_CORE,&rl);
    cplus_safe_set_constraint_mode(0);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop);
    memset(buf,CAN,sizeof buf);
    memset(buf+off,'B', 12); buf[off+12]=0;
    printf("off=%d CAP=%d dm=%d n=1 src[0..2]=%02x%02x%02x\n", off, CAP, CAP+1, buf[off],buf[off+1],buf[off+2]);
    rc = cplus_sc_strncpy_s((char*)buf, (rsize_t)(CAP+1), (const char*)(buf+off), 1);
    printf("rc=%d\n", rc);
    for(i=CAP;i<CAP+16;i++) printf("buf[%zu]=%02x ", i, buf[i]);
    printf("\n");
    /* check canary */
    for(i=CAP;i<320;i++) if(buf[i]!=CAN){printf("CORRUPT first_bad=%zu\n",i);break;}
    return 0;
}

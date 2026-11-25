#include <assert.h>
#include <stdlib.h>
#include <string.h>
int cplus_rand(void); void cplus_srand(unsigned int); int cplus_atexit(void(*)(void));
char *cplus_getenv(const char*); int cplus_system(const char*);

static int g_atexit_ran=0;
static void on_exit_cb(void){ g_atexit_ran=1; }

int main(void){
    cplus_srand(1);
    int a=cplus_rand(), b=cplus_rand();
    cplus_srand(1);
    assert(cplus_rand()==a);
    assert(cplus_rand()==b);
    for(int i=0;i<10;i++) (void)cplus_rand();

    assert(cplus_atexit(on_exit_cb)==0);
    /* do not call exit/abort in test process */
    char *p=cplus_getenv("PATH");
    assert(p==NULL || strlen(p)>0);
    /* safe system command */
    assert(cplus_system("true")==0);
    for(int i=0;i<10;i++) assert(cplus_system("true")==0);
    return 0;
}

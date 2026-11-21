#include <assert.h>
#include <signal.h>
#include <stddef.h>
void (*cplus_signal(int, void(*)(int)))(int);
int cplus_raise(int);
static volatile sig_atomic_t got=0;
static void h(int s){ (void)s; got=1; }
int main(void){
    for(int i=0;i<10;i++){ got=0; cplus_signal(SIGUSR1,h); cplus_raise(SIGUSR1); assert(got==1); }
    return 0;
}

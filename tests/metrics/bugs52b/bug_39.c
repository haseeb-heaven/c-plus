#include <stdlib.h>
typedef void(*cb_t)(void*ctx);
cb_t global_cb=NULL; void* global_ctx=NULL;
void register_cb(cb_t cb,void*ctx){global_cb=cb;global_ctx=ctx;}
void on_event(void*ctx){ *(int*)ctx+=1; }
int main(void){ int*state=(int*)malloc(sizeof(int)); *state=0; register_cb(on_event,state); free(state); if(global_cb) global_cb(global_ctx); return 0; }

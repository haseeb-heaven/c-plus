#include <dlfcn.h>
typedef void(*plugin_fn)(void);
int main(void){ void*h=dlopen("./libplugin.so",RTLD_LAZY); if(h){ plugin_fn fn=(plugin_fn)dlsym(h,"run_plugin"); dlclose(h); if(fn) fn(); } return 0; }

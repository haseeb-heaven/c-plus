#include "cplus/cplus.h"
int main(void){ int a[]={2,3,5,7,11,13,17}; auto s=0; for(auto v : a) s+=v; print("RANGE",a.len,countof(a),s);return 0; }

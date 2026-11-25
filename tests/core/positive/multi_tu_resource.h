#ifndef CPLUS_CORE_MULTI_TU_RESOURCE_H
#define CPLUS_CORE_MULTI_TU_RESOURCE_H
#include "cplus/cplus.h"
struct SharedR { int id; };
typedef struct SharedR SharedR;
void SharedR_drop(SharedR *r);
SharedR SharedR_make(int id);
int SharedR_value(SharedR *r);
#endif

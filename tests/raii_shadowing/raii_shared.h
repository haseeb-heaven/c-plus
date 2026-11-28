#ifndef RAII_SHADOW_SHARED_H
#define RAII_SHADOW_SHARED_H
#include "cplus/cplus.h"
struct R { int id; };
void R_drop(struct R *self);
struct R make_r(int id);
#endif

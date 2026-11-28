#include "multi_tu_resource.h"
void SharedR_drop(SharedR *r) { if (r->id) r->id = 0; }
SharedR SharedR_make(int id) { SharedR r; r.id = id; return r; }
int SharedR_value(SharedR *r) { return r->id; }

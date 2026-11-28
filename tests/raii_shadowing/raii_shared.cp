#include "raii_shared.h"
#include <stdio.h>
void R_drop(struct R *self) { if (self->id) { print("DROP", self->id); self->id = 0; } }
struct R make_r(int id) { struct R r = {id}; return r; }

/* Demo I: deterministic cleanup across loops and early returns.
 *
 * Every Mark is destroyed when its scope ends, including the continue path
 * and the early return path. */
#include <stdio.h>

resource struct Mark {
    int id;
};

void Mark_drop(Mark *self) {
    if (self->id != 0) {
        printf("drop %d\n", self->id);
        self->id = 0;
    }
}

Mark Mark_make(int id) {
    Mark m;
    m.id = id;
    return m;
}

static int early(int id) {
    Mark m = Mark_make(id);
    if (id == 2)
        return -1;
    return id;
}

int main(void) {
    int i;
    for (i = 1; i <= 3; i++) {
        Mark m = Mark_make(i);
        if (i == 2)
            continue;
        (void)m;
    }
    printf("early=%d\n", early(2));
    return 0;
}

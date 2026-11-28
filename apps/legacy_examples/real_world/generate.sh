#!/bin/sh
# Generate the remaining deterministic real-world C+ example programs.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
D=apps/legacy_examples/real_world

# --- playable deterministic game: snake board logic -----------------------
cat > "$D/snake_game.cp" <<'EOF'
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
/* Deterministic snake board: fixed start, scripted directions. */
#define W 8
#define H 8
static int step(int bx, int by, int dx, int dy) {
    error e = error();
    int nx = bx + dx, ny = by + dy;
    if (nx < 0 || nx >= W || ny < 0 || ny >= H)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "snake: wall collision"), -1;
    return nx + ny * W;
}
int main(void) {
    int p = step(3, 3, 1, 0);
    int bad = step(0, 0, -1, 0);
    print("SNAKE_GAME", p, bad);
    return (p == 28 && bad == -1) ? 0 : 1;
}
EOF

# --- desktop app: menu/form state machine ---------------------------------
cat > "$D/desktop_app.cp" <<'EOF'
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
/* Desktop-style app: bounded menu selection and form fields. */
static int choose(int menu_max, int sel) {
    error e = error();
    if (menu_max <= 0 || menu_max > 100 || sel < 0 || sel >= menu_max)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "desktop: invalid menu selection"), -1;
    return sel;
}
int main(void) {
    string title = string("Settings");
    int ok = choose(4, 2);
    int bad = choose(4, 9);
    print("DESKTOP_APP", title.size(), ok, bad);
    title.drop();
    return (ok == 2 && bad == -1) ? 0 : 1;
}
EOF

# --- web app: request routing ----------------------------------------------
cat > "$D/web_app.cp" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
/* Web app: bounded route matching, no sockets (deterministic core). */
static int route(const char *path) {
    error e = error();
    if (path == 0 || strlen(path) > 128)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "web: bad path"), -1;
    if (strcmp(path, "/") == 0) return 200;
    if (strcmp(path, "/health") == 0) return 204;
    return 404;
}
int main(void) {
    print("WEB_APP", route("/"), route("/health"), route("/x"), route(""));
    return (route("/") == 200 && route("/health") == 204 &&
            route("/x") == 404 && route("") == 404) ? 0 : 1;
}
EOF

# --- server: request queue with bounded capacity ---------------------------
cat > "$D/server_app.cp" <<'EOF'
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
/* Server: bounded request queue (grow-only vector, no real network). */
static int accept_req(vector *q, int id, int cap) {
    error e = error();
    if (id < 0 || cap <= 0 || cap > 10000 || q->size() >= (size_t)cap)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_RESOURCE,
                              "server: queue full/invalid"), -1;
    return q->push_back(id);
}
int main(void) {
    vector q = vector();
    int a = accept_req(&q, 1, 4);
    int b = accept_req(&q, 2, 4);
    int bad = accept_req(&q, -1, 4);
    print("SERVER_APP", a, b, q.size(), bad);
    q.drop();
    return (a == 0 && b == 0 && bad == -1) ? 0 : 1;
}
EOF

# --- database engine: in-memory table with CRUD + query --------------------
cat > "$D/database_engine.cp" <<'EOF'
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
/* Database engine: bounded table with duplicate-key refusal and sum query. */
static int insert(intmap *idx, vector *vals, int key, int val) {
    error e = error();
    int dummy = 0;
    if (key < 0 || key > 100000 || val < 0 || val > 1000000)
        return error_fail_cat(&e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                              "db: value out of range"), -1;
    if (idx->contains(key))
        return error_fail_cat(&e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                              "db: duplicate key"), -1;
    if (vals->push_back(val) != 0) return -1;
    if (idx->put(key, val) != 0) return -1;
    (void)dummy;
    return 0;
}
int main(void) {
    intmap idx = intmap();
    vector vals = vector();
    int a = insert(&idx, &vals, 1, 10);
    int b = insert(&idx, &vals, 2, 20);
    int dup = insert(&idx, &vals, 1, 99);
    int bad = insert(&idx, &vals, -5, 10);
    int sum = 0; size_t i;
    for (i = 0; i < vals.size(); i++) sum += vals.at(i);
    print("DATABASE_ENGINE", a, b, dup, bad, sum);
    vals.drop();
    return (a == 0 && b == 0 && dup == -1 && bad == -1 && sum == 30) ? 0 : 1;
}
EOF
echo "GENERATED real-world examples"

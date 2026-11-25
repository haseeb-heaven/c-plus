/* engine_loop.cp -- fixed-timestep game-loop simulator with deterministic input.
 *
 * NOT a game engine: a small, reproducible state machine.  Every tick is driven
 * by an explicit input token from argv and a tick counter -- there is no wall
 * clock, no time() call and no hidden state, so identical argv always produces
 * an identical transcript.
 *
 * Usage: engine_loop <token> [token ...]
 *   tokens: N = no input, L = move left, R = move right, F = fire
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/engine/engine_loop.cp --strict \
 *       --sanitize=none -I. -o examples/expanded/engine/.build/engine_loop
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

#define MAX_TICKS 16
#define LANE_MIN (-8)
#define LANE_MAX 8
#define TARGET_COUNT 3

#define IN_NONE 0
#define IN_LEFT (-1)
#define IN_RIGHT 1
#define IN_FIRE 2

static unsigned long long rng_state = 0xabcd1234ULL;

static int rng_next(int lo, int hi) {
    unsigned int bits;
    unsigned int span;
    if (hi <= lo) return lo;
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    bits = (unsigned int)((rng_state >> 33) & 0xffffffffu);
    span = (unsigned int)(hi - lo + 1);
    if (span == 0u) return lo;
    return lo + (int)(bits % span);
}

static const char *input_name(int cmd) {
    if (cmd == IN_LEFT) return "L";
    if (cmd == IN_RIGHT) return "R";
    if (cmd == IN_FIRE) return "F";
    return "N";
}

static int parse_token(const char *token, int *out) {
    if (token == NULL || strlen(token) != 1) return -1;
    if (strcmp(token, "L") == 0) { *out = IN_LEFT; return 0; }
    if (strcmp(token, "R") == 0) { *out = IN_RIGHT; return 0; }
    if (strcmp(token, "F") == 0) { *out = IN_FIRE; return 0; }
    if (strcmp(token, "N") == 0) { *out = IN_NONE; return 0; }
    return -1;
}

typedef struct {
    int x;
    int lives;
    int score;
    int hits;
    int collisions;
    int hazard;
    int targets[TARGET_COUNT];
} Game;

static int checksum(Game *g) {
    int sum = g->score * 100 + g->hits * 10 + g->collisions;
    int i;
    sum += g->x + g->lives * 7 + g->hazard;
    for (i = 0; i < TARGET_COUNT; ++i)
        sum += g->targets[i];
    return sum;
}

int main(int argc, char **argv) {
    vector inputs = vector();
    error err = error();
    Game g;
    int tick;
    int n;
    int i;

    if (argc < 2) {
        err.set(CPLUS_EXIT_UNSUPPORTED,
                "usage: engine_loop <N|L|R|F> [token ...]");
        print("ERROR", err.what(), "code=", err.code());
        return 2;
    }
    n = argc - 1;
    if (n > MAX_TICKS) n = MAX_TICKS;

    for (i = 0; i < n; ++i) {
        int cmd = 0;
        char *token = NULL;
        /* argv is a pointer parameter with unverifiable extent; copy the
         * element into a local with a checked, sized copy instead of a direct
         * subscript (strict mode rejects argv[variable]). */
        __builtin_memcpy(&token, argv + i + 1, sizeof(token));
        if (parse_token(token, &cmd) != 0) {
            err.set(CPLUS_EXIT_UNSUPPORTED, "invalid input token");
            print("ERROR", err.what(), "token=", token,
                  "code=", err.code());
            return 2;
        }
        if (vector_push_back(&inputs, cmd) != 0) {
            err.set(CPLUS_EXIT_RESOURCE, "input queue allocation failed");
            print("ERROR", err.what(), "code=", err.code());
            return 2;
        }
    }

    rng_state = 0xabcd1234ULL;
    g.x = 0;
    g.lives = 3;
    g.score = 0;
    g.hits = 0;
    g.collisions = 0;
    g.hazard = 7;
    g.targets[0] = -5;
    g.targets[1] = 0;
    g.targets[2] = 5;

    print("LOOP ticks=", n, "seed=0xabcd1234 lane=", LANE_MIN, "..", LANE_MAX);
    for (tick = 0; tick < n; ++tick) {
        int cmd = IN_NONE;
        if (vector_at_checked(&inputs, (size_t)tick, &cmd) != 0) {
            err.set(CPLUS_EXIT_BOUNDS, "input queue read out of range");
            print("ERROR", err.what(), "code=", err.code());
            return 2;
        }

        /* input + state update: only the movement tokens change position. */
        if (cmd == IN_LEFT) --g.x;
        if (cmd == IN_RIGHT) ++g.x;
        if (g.x < LANE_MIN) g.x = LANE_MIN;
        if (g.x > LANE_MAX) g.x = LANE_MAX;

        /* hazard moves deterministically by tick, never by wall time */
        --g.hazard;
        if (g.hazard < LANE_MIN) g.hazard = LANE_MAX;

        /* collision */
        if (g.x == g.hazard) {
            g.lives -= 1;
            g.collisions += 1;
            g.x = 0;
            print("COLLISION tick=", tick, "lives=", g.lives);
        }

        /* fire + scoring */
        if (cmd == IN_FIRE) {
            int hit_index = -1;
            for (i = 0; i < TARGET_COUNT; ++i) {
                int distance = g.x - g.targets[i];
                if (distance < 0) distance = -distance;
                if (distance <= 1) { hit_index = i; break; }
            }
            if (hit_index >= 0) {
                g.score += 10;
                g.hits += 1;
                g.targets[hit_index] = rng_next(LANE_MIN, LANE_MAX);
                print("HIT tick=", tick, "target=", hit_index,
                      "score=", g.score);
            } else {
                print("MISS tick=", tick, "x=", g.x);
            }
        }

        print("STATE tick=", tick, "input=", input_name(cmd), "x=", g.x,
              "hazard=", g.hazard, "score=", g.score, "lives=", g.lives,
              "hits=", g.hits);
    }

    print("SUMMARY ticks=", n, "score=", g.score, "hits=", g.hits,
          "collisions=", g.collisions, "lives=", g.lives,
          "checksum=", checksum(&g));
    return 0;
}

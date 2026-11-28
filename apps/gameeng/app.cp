/* Real game-engine headless app (not a demo) on the stdlib/gameeng.hp core.
 *
 * Runs a headless game loop: adds entities, steps the world deterministically
 * and prints each entity's state per frame.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/gameeng.hp"

int main(void) {
    game_world w;
    int idx, x, y, i;

    game_world_init(&w);
    idx = game_add_entity(&w, 5, -3, 10);
    printf("GAMEENG_APP add0 idx=%d\n", idx);
    idx = game_add_entity(&w, -4, 7, 5);
    printf("GAMEENG_APP add1 idx=%d\n", idx);

    for (i = 0; i < 4; i++) {
        game_step(&w, 0.25f);
        x = 0; y = 0;
        game_entity_pos(&w, 0, &x, &y);
        printf("GAMEENG_APP frame=%d ent0=(%d,%d)\n", i + 1, x, y);
    }
    idx = game_add_entity(&w, 99, 99, 1);
    printf("GAMEENG_APP kill idx=%d rc=%d\n", idx, game_kill(&w, idx));
    x = -1; y = -1;
    game_entity_pos(&w, idx, &x, &y);
    printf("GAMEENG_APP killed_pos_rc=%d\n", (x == -1 && y == -1) ? 1 : 0);
    return 0;
}

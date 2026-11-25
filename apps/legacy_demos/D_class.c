/* Demo D: lightweight struct-based class, lowered to plain functions. */
#include <stdio.h>

class Player {
    int health;
    int score;

    void damage(int amount) {
        self->health -= amount;
    }

    void heal(int amount) {
        self->health += amount;
    }

    int get_health(void) {
        return self->health;
    }
};

int main(void) {
    Player player = {100, 0};
    player.damage(30);
    printf("after damage: %d\n", player.get_health());
    player.heal(10);
    printf("after heal: %d\n", player.get_health());

    Player *view = &player;
    view->damage(5);
    printf("through pointer: %d\n", view->get_health());
    return 0;
}

#include <stdio.h>

class Player {
    int health;
    int score;

    void damage(int amount) {
        self->health -= amount;
    }

    int get_health(void) {
        return self->health;
    }

    void add_score(int points) {
        self->score += points;
    }
};

int main(void) {
    Player p = {100, 0};
    p.damage(30);
    p.add_score(7);
    printf("health=%d score=%d\n", p.get_health(), p.score);

    Player *q = &p;
    q->damage(5);
    printf("health=%d\n", q->get_health());
    return 0;
}

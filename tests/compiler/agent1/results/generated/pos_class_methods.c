#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
#include <stdio.h>

struct Player {
int health;
int score;
};
typedef struct Player Player;
#line 7 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
void Player_damage(Player *self, int amount)
#line 7 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
{
        self->health -= amount;
    }
#line 11 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
int Player_get_health(Player *self)
#line 11 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
{
        return self->health;
    }
#line 15 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
void Player_add_score(Player *self, int points)
#line 15 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"
{
        self->score += points;
    }
#line 20 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_methods.c"


int main(void) {
    Player p = {100, 0};
    Player_damage(&p, 30);
    Player_add_score(&p, 7);
    printf("health=%d score=%d\n", Player_get_health(&p), p.score);

    Player *q = &p;
    Player_damage(q, 5);
    printf("health=%d\n", Player_get_health(q));
    return 0;
}

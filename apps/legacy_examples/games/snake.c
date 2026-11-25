#include <stdio.h>
#include <stdlib.h>
// #include <windows.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

/* POSIX shim for the original Windows _kbhit/_getch input. */
static struct termios snake_orig_termios;
static void snake_raw_mode(int on) {
  if (on) {
    tcgetattr(STDIN_FILENO, &snake_orig_termios);
    struct termios raw = snake_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  } else {
    tcsetattr(STDIN_FILENO, TCSANOW, &snake_orig_termios);
  }
}
static int posix_kbhit(void) {
  fd_set fds;
  struct timeval tv = {0, 0};
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}
static int posix_getch(void) { return getchar(); }
#define _kbhit posix_kbhit
#define _getch posix_getch

#define WIDTH 20
#define HEIGHT 20
#define UP 72
#define DOWN 80
#define LEFT 75
#define RIGHT 77

int gameover;
int score;
int tailX[100], tailY[100];
int tailLength;
int fruitX, fruitY;
int headX, headY;
int dir;

void Setup() {
    snake_raw_mode(1);
    gameover = 0;
    score = 0;
    dir = RIGHT;
    headX = WIDTH / 2;
    headY = HEIGHT / 2;

    // Place the fruit randomly on the screen
    srand(time(NULL));
    fruitX = rand() % WIDTH;
    fruitY = rand() % HEIGHT;
}

void Draw() {
    system("cls"); // Clear the console

    // Draw the top wall
    for (int i = 0; i < WIDTH + 2; i++) {
        printf("#");
    }
    printf("\n");

    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            if (j == 0) {
                printf("#");
            }

            if (i == headY && j == headX) {
                printf("O"); // Draw the snake's head
            } else if (i == fruitY && j == fruitX) {
                printf("F"); // Draw the fruit
            } else {
                int isTail = 0;
                for (int k = 0; k < tailLength; k++) {
                    if (tailX[k] == j && tailY[k] == i) {
                        printf("o"); // Draw the snake's tail
                        isTail = 1;
                        break;
                    }
                }
                if (!isTail) {
                    printf(" ");
                }
            }

            if (j == WIDTH - 1) {
                printf("#");
            }
        }
        printf("\n");
    }

    // Draw the bottom wall
    for (int i = 0; i < WIDTH + 2; i++) {
        printf("#");
    }
    printf("\n");

    printf("Score: %d\n", score);
}

void Input() {
    if (_kbhit()) {
        switch (_getch()) {
            case 'a':
                dir = LEFT;
                break;
            case 'd':
                dir = RIGHT;
                break;
            case 'w':
                dir = UP;
                break;
            case 's':
                dir = DOWN;
                break;
            case 'x':
                snake_raw_mode(0);
                gameover = 1;
                break;
        }
    }
}

void Logic() {
    int prevX = tailX[0];
    int prevY = tailY[0];
    int prev2X, prev2Y;
    tailX[0] = headX;
    tailY[0] = headY;

    for (int i = 1; i < tailLength; i++) {
        prev2X = tailX[i];
        prev2Y = tailY[i];
        tailX[i] = prevX;
        tailY[i] = prevY;
        prevX = prev2X;
        prevY = prev2Y;
    }

    switch (dir) {
        case LEFT:
            headX--;
            break;
        case RIGHT:
            headX++;
            break;
        case UP:
            headY--;
            break;
        case DOWN:
            headY++;
            break;
    }

    // Check for collision with walls
    if (headX < 0 || headX >= WIDTH || headY < 0 || headY >= HEIGHT) {
        gameover = 1;
    }

    // Check for collision with the tail
    for (int i = 0; i < tailLength; i++) {
        if (tailX[i] == headX && tailY[i] == headY) {
            gameover = 1;
        }
    }

    // Check for collision with fruit
    if (headX == fruitX && headY == fruitY) {
        score++;
        tailLength++;
        // Place the fruit randomly on the screen
        srand(time(NULL));
        fruitX = rand() % WIDTH;
        fruitY = rand() % HEIGHT;
    }
}

int main() {
    Setup();

    while (!gameover) {
        Draw();
        Input();
        Logic();
        sleep(10); // Delay the game for a short period
    }

    printf("Game Over! Your Score: %d\n", score);

    return 0;
}

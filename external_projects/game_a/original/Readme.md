# Snake

Classic Snake game in the terminal, in C.

A small, self-contained demo written in **pure C** — no external libraries,
just the standard library and POSIX. Part of the [Corg-Labs](https://github.com/Corg-Labs)
collection of single-file C programs.

---

## How It Works

1. Terminal is put into non-blocking raw mode with termios
2. The snake body is stored as an array of segments
3. Eating food grows the snake and bumps the score
4. Hitting a wall or yourself ends the game

---

# Tutorial

This tutorial walks through the key concepts and implementation stages of `snake.c`, a single-file terminal Snake game written in pure C with no external dependencies.

## 1. Terminal Raw Mode and Non-Blocking Input

Before the game loop starts, the terminal is switched into raw mode so keypresses are read immediately without waiting for Enter, and echo is suppressed so characters typed do not appear on screen. `O_NONBLOCK` is set on stdin so the read loop never stalls waiting for a key.

```c
static void raw_on(void){
    tcgetattr(0,&orig); struct termios t=orig;
    t.c_lflag&=~(ICANON|ECHO); tcsetattr(0,TCSANOW,&t);
    fcntl(0,F_SETFL,fcntl(0,F_GETFL)|O_NONBLOCK);
}
static void raw_off(void){ tcsetattr(0,TCSANOW,&orig); printf("\033[?25h"); }
```

`raw_off` restores the original terminal settings and re-enables the cursor (`\033[?25h`) when the game exits.

## 2. The Snake Body: Parallel Arrays

The snake's body is represented as two parallel integer arrays — one for x-coordinates and one for y-coordinates — with a `len` counter tracking how many segments are currently alive.

```c
int sx[W*H], sy[W*H], len=4;
for(int i=0;i<len;i++){ sx[i]=W/2-i; sy[i]=H/2; }
```

Index `0` is always the head. The maximum possible length is `W*H` (every cell on the board), so the arrays are sized accordingly. The initial snake of length 4 is placed horizontally at the centre of the board.

## 3. Input Handling and Direction Locking

Each frame, all pending bytes on stdin are drained in a tight `while(read(...))` loop. Arrow keys arrive as a three-byte ANSI escape sequence (`\033[A/B/C/D`). WASD keys are handled directly as single characters. A direction can only change on the axis that is currently idle, preventing a 180° reversal that would immediately cause self-collision.

```c
while(read(0,&c,1)==1){
    if(c=='\033'){ char a,b; if(read(0,&a,1)==1&&read(0,&b,1)==1){
        if(b=='A'&&dy==0){dx=0;dy=-1;} else if(b=='B'&&dy==0){dx=0;dy=1;}
        else if(b=='C'&&dx==0){dx=1;dy=0;} else if(b=='D'&&dx==0){dx=-1;dy=0;} } }
    else if((c=='w'||c=='W')&&dy==0){dx=0;dy=-1;}
    else if((c=='s'||c=='S')&&dy==0){dx=0;dy=1;}
    else if((c=='d'||c=='D')&&dx==0){dx=1;dy=0;}
    else if((c=='a'||c=='A')&&dx==0){dx=-1;dy=0;}
    else if(c=='q') dead=1;
}
```

The guard `dy==0` (move only when not already moving vertically) prevents reversals on the y-axis, and `dx==0` does the same for x.

## 4. Collision Detection and Snake Advancement

The new head position is computed, checked against wall boundaries, then checked against every existing segment. If either check fails, `dead` is set and the loop exits.

```c
int nx=sx[0]+dx, ny=sy[0]+dy;
if(nx<0||ny<0||nx>=W||ny>=H){ dead=1; break; }
for(int i=0;i<len;i++) if(sx[i]==nx&&sy[i]==ny){ dead=1; }
```

If movement is safe, the body is shifted one position tail-to-head (a right-shift by one index), and the new head coordinates are written at index 0:

```c
for(int i=len;i>0;i--){ sx[i]=sx[i-1]; sy[i]=sy[i-1]; }
sx[0]=nx; sy[0]=ny;
```

This shift naturally discards the old tail because the loop stops at index `len`, and the tail slot at `len` is simply never read again — unless `len` grows.

## 5. Food Consumption and Growth

After the body is advanced, the head position is compared to the food cell. A match increments `len` (so the tail slot written during the shift is now within bounds and the snake is one cell longer), adds 10 to the score, and places new food at a random position.

```c
if(nx==fx&&ny==fy){ len++; score+=10; fx=rand()%W; fy=rand()%H; }
```

Because the body-shift loop already wrote the old tail position into `sx[len]`/`sy[len]`, incrementing `len` is all that is needed — no extra copy step.

## 6. Rendering with ANSI Escape Codes

The board is redrawn every tick using ANSI escape codes. `\033[H` moves the cursor back to the top-left corner so the new frame overwrites the previous one without scrolling. The cursor is hidden at game start (`\033[?25l`) to avoid flicker.

```c
printf("\033[H");
for(int y=0;y<H;y++){
    for(int x=0;x<W;x++){
        int body=0; for(int i=0;i<len;i++) if(sx[i]==x&&sy[i]==y){body=(i==0)?2:1;break;}
        if(body==2) putchar('@'); else if(body) putchar('o');
        else if(x==fx&&y==fy) putchar('*'); else putchar('.');
    }
    putchar('\n');
}
printf("score: %d   (WASD/arrows, q=quit)\n", score);
fflush(stdout);
usleep(90000);
```

Each cell is classified in a single inner scan of the snake array. The head prints as `@`, body segments as `o`, food as `*`, and empty cells as `.`. `usleep(90000)` caps the frame rate at roughly 11 fps, controlling game speed.

---

## Build

```
gcc snake.c -o snake
```

## Run

```
./snake
```

## Controls

**WASD** or **arrow keys** to move, **q** to quit.

/* snake_modern.cp - Modern C+ conversion of the classic terminal Snake game.
 *
 * Provenance: external_projects/game_a/original/snake.c (Corg-Labs "Snake").
 * The original is retained byte-for-byte in .../original/. This file is its
 * conservative C+ conversion: identical CLI (no arguments), identical stdin
 * protocol (non-blocking raw-byte drain, WASD/arrows, 'q' quits), identical
 * stdout byte stream and identical process exit code for every fixed input.
 *
 * Build (from repository root):
 *   ./build/c+ build external_projects/game_a/cplus/snake_modern.cp \
 *       --strict --sanitize=none -I. \
 *       -Istdlib/safe -Ilibc/install-include -- \
 *       build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread \
 *       -o external_projects/game_a/cplus/snake_modern
 *
 * What is modernized (see CONVERSION.md for line-level rationale):
 *   - two fixed-size parallel int arrays -> two C+ `vector` containers with
 *     bounds-checked at()/insert()/pop_back() and deterministic drop() (RAII);
 *   - terminal raw-mode lifecycle -> `RawModeGuard` class with drop() (RAII);
 *   - locals read() into are zero-initialised (no uninitialised reads);
 *   - ordinary printf is routed through the checked safe_c.h layer (compile
 *     time format-attribute checking) by linking the hardened libc archives.
 * Exported surface is unchanged: `int main(void)` is still the only external
 * symbol; raw_on/raw_off/orig remain file-static as in the original.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

/* safe_c.h must come first: the integration test uses this ordering to avoid
 * a token collision between its libc declarations and the cplus runtime. */
#include "stdlib/safe/safe_c.h"
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

#define W 40
#define H 20

static struct termios orig;
static void raw_on(void){
    tcgetattr(0,&orig); struct termios t=orig;
    t.c_lflag&=~(ICANON|ECHO); tcsetattr(0,TCSANOW,&t);
    fcntl(0,F_SETFL,fcntl(0,F_GETFL)|O_NONBLOCK);
}
static void raw_off(void){ tcsetattr(0,TCSANOW,&orig); printf("\033[?25h"); }

/* RAII wrapper around the original raw_on()/raw_off() pair. restore_now()
 * keeps the original output order (cursor shown BEFORE the final print) while
 * drop() guarantees the terminal is restored on any early return. */
class RawModeGuard {
    int armed;

    RawModeGuard(void) { raw_on(); this->armed = 1; }

    void restore_now(void) {
        if (this->armed) { raw_off(); this->armed = 0; }
    }

    void drop(void) {
        if (this->armed) { raw_off(); this->armed = 0; }
    }
};

int main(void){
    srand((unsigned)time(NULL));

    /* C+ owned, bounds-checked containers replace `int sx[W*H], sy[W*H]`.
     * Reserve the original fixed capacity up front for clarity; push_back,
     * insert and pop_back keep the exact body-shift semantics of the source. */
    vector sx = vector();
    vector sy = vector();
    if (sx.reserve(W*H) != 0 || sy.reserve(W*H) != 0) return 1;
    for (int i = 0; i < 4; i++) {
        sx.push_back(W/2 - i);
        sy.push_back(H/2);
    }

    int dx=1,dy=0, fx=rand()%W, fy=rand()%H, score=0, dead=0;
    RawModeGuard guard = RawModeGuard();
    printf("\033[2J\033[?25l");
    while(!dead){
        /* Zero-initialised: strict mode rejects reads of uninitialised locals.
         * read() still writes before every use; behaviour is unchanged. */
        char c = 0;
        while(read(0,&c,1)==1){
            if(c=='\033'){ char a = 0, b = 0; if(read(0,&a,1)==1&&read(0,&b,1)==1){
                if(b=='A'&&dy==0){dx=0;dy=-1;} else if(b=='B'&&dy==0){dx=0;dy=1;}
                else if(b=='C'&&dx==0){dx=1;dy=0;} else if(b=='D'&&dx==0){dx=-1;dy=0;} } }
            else if((c=='w'||c=='W')&&dy==0){dx=0;dy=-1;}
            else if((c=='s'||c=='S')&&dy==0){dx=0;dy=1;}
            else if((c=='d'||c=='D')&&dx==0){dx=1;dy=0;}
            else if((c=='a'||c=='A')&&dx==0){dx=-1;dy=0;}
            else if(c=='q') dead=1;
        }
        int nx = sx.at(0)+dx, ny = sy.at(0)+dy;
        if(nx<0||ny<0||nx>=W||ny>=H){ dead=1; break; }
        for (size_t i = 0; i < sx.size(); i++)
            if(sx.at(i)==nx && sy.at(i)==ny){ dead=1; }
        if(dead) break;
        /* Original: shift tail-to-head, write head at index 0, then grow on
         * food. insert(0, head) does the shift+write; pop_back() discards the
         * old tail unless food was eaten (which keeps the extra segment). */
        if (sx.insert(0,nx) != 0 || sy.insert(0,ny) != 0){ dead=1; break; }
        if(nx==fx&&ny==fy){ score+=10; fx=rand()%W; fy=rand()%H; }
        else { sx.pop_back(NULL); sy.pop_back(NULL); }
        printf("\033[H");
        for(int y=0;y<H;y++){
            for(int x=0;x<W;x++){
                int body=0; for (size_t i = 0; i < sx.size(); i++)
                    if(sx.at(i)==x && sy.at(i)==y){body=(i==0)?2:1;break;}
                if(body==2) putchar('@'); else if(body) putchar('o');
                else if(x==fx&&y==fy) putchar('*'); else putchar('.');
            }
            putchar('\n');
        }
        printf("score: %d   (WASD/arrows, q=quit)\n", score);
        fflush(stdout);
        usleep(90000);
    }
    guard.restore_now();
    printf("\033[2J\033[H Game over! Score: %d\n", score);
    sx.drop();
    sy.drop();
    return 0;
}

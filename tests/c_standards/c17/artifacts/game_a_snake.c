/* snake.c - Classic Snake in the terminal (WASD / arrow keys).
 * Build: gcc snake.c -o snake */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

#define W 40
#define H 20

static struct termios orig;
static void raw_on(void){
    tcgetattr(0,&orig); struct termios t=orig;
    t.c_lflag&=~(ICANON|ECHO); tcsetattr(0,TCSANOW,&t);
    fcntl(0,F_SETFL,fcntl(0,F_GETFL)|O_NONBLOCK);
}
static void raw_off(void){ tcsetattr(0,TCSANOW,&orig); printf("\033[?25h"); }

int main(void){
    srand((unsigned)time(NULL));
    int sx[W*H], sy[W*H], len=4;
    for(int i=0;i<len;i++){ sx[i]=W/2-i; sy[i]=H/2; }
    int dx=1,dy=0, fx=rand()%W, fy=rand()%H, score=0, dead=0;
    raw_on(); printf("\033[2J\033[?25l");
    while(!dead){
        char c;
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
        int nx=sx[0]+dx, ny=sy[0]+dy;
        if(nx<0||ny<0||nx>=W||ny>=H){ dead=1; break; }
        for(int i=0;i<len;i++) if(sx[i]==nx&&sy[i]==ny){ dead=1; }
        if(dead) break;
        for(int i=len;i>0;i--){ sx[i]=sx[i-1]; sy[i]=sy[i-1]; }
        sx[0]=nx; sy[0]=ny;
        if(nx==fx&&ny==fy){ len++; score+=10; fx=rand()%W; fy=rand()%H; }
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
    }
    raw_off(); printf("\033[2J\033[H Game over! Score: %d\n", score);
    return 0;
}

/* tiles.c - 2048 sliding-tile game. WASD/arrows to move, q quit.
 * Build: gcc tiles.c -o tiles */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

static struct termios orig;
static void raw_on(void){ tcgetattr(0,&orig); struct termios t=orig;
    t.c_lflag&=~(ICANON|ECHO); tcsetattr(0,TCSANOW,&t);
    fcntl(0,F_SETFL,fcntl(0,F_GETFL)|O_NONBLOCK); }
static void raw_off(void){ tcsetattr(0,TCSANOW,&orig); printf("\033[?25h"); }

static int g[4][4], score;

static void spawn(void){
    int e[16],n=0;
    for(int i=0;i<16;i++) if(!g[i/4][i%4]) e[n++]=i;
    if(!n) return;
    int p=e[rand()%n]; g[p/4][p%4]=(rand()%10==0)?4:2;
}
/* slide+merge one row toward index 0 */
static int slide(int *r){
    int moved=0,tmp[4]={0,0,0,0},k=0;
    for(int i=0;i<4;i++) if(r[i]) tmp[k++]=r[i];
    for(int i=0;i<3;i++) if(tmp[i]&&tmp[i]==tmp[i+1]){ tmp[i]*=2; score+=tmp[i]; tmp[i+1]=0; }
    int out[4]={0,0,0,0}; k=0;
    for(int i=0;i<4;i++) if(tmp[i]) out[k++]=tmp[i];
    for(int i=0;i<4;i++){ if(r[i]!=out[i])moved=1; r[i]=out[i]; }
    return moved;
}
static int move(int dir){ /* 0=left 1=right 2=up 3=down */
    int moved=0;
    for(int i=0;i<4;i++){
        int r[4];
        for(int j=0;j<4;j++){
            if(dir==0) r[j]=g[i][j];
            else if(dir==1) r[j]=g[i][3-j];
            else if(dir==2) r[j]=g[j][i];
            else r[j]=g[3-j][i];
        }
        if(slide(r)) moved=1;
        for(int j=0;j<4;j++){
            if(dir==0) g[i][j]=r[j];
            else if(dir==1) g[i][3-j]=r[j];
            else if(dir==2) g[j][i]=r[j];
            else g[3-j][i]=r[j];
        }
    }
    return moved;
}
int main(void){
    srand((unsigned)time(NULL));
    memset(g,0,sizeof(g)); score=0; spawn(); spawn();
    raw_on(); printf("\033[2J\033[?25l");
    int run=1;
    while(run){
        printf("\033[H 2048   score: %d\n\n", score);
        for(int i=0;i<4;i++){
            for(int j=0;j<4;j++){ if(g[i][j]) printf("%6d", g[i][j]); else printf("     ."); }
            printf("\n\n");
        }
        printf(" WASD/arrows to move, q to quit\n");
        fflush(stdout);
        int did=0; char c;
        while(!did){
            if(read(0,&c,1)==1){
                int d=-1;
                if(c=='a')d=0; else if(c=='d')d=1; else if(c=='w')d=2; else if(c=='s')d=3;
                else if(c=='q'){run=0;break;}
                else if(c=='\033'){ char a,b; if(read(0,&a,1)==1&&read(0,&b,1)==1){
                    if(b=='D')d=0; else if(b=='C')d=1; else if(b=='A')d=2; else if(b=='B')d=3; } }
                if(d>=0){ if(move(d)){ spawn(); } did=1; }
            } else usleep(10000);
        }
    }
    raw_off(); printf("\033[2J\033[H Final score: %d\n", score);
    return 0;
}

#include <assert.h>
#include <string.h>
#include <stdio.h>

/* stdio position/stream extras under test */
int cplus_setvbuf(FILE*,char*,int,size_t);
void cplus_setbuf(FILE*,char*);
int cplus_fgetpos(FILE*,fpos_t*);
int cplus_fsetpos(FILE*,const fpos_t*);
int cplus_ungetc(int,FILE*);
int cplus_fileno(FILE*);
FILE *cplus_fdopen(int,const char*);
FILE *cplus_freopen(const char*,const char*,FILE*);
FILE *cplus_tmpfile(void);

/* host-libc helpers reused for setup/verification */
FILE *cplus_fopen(const char*,const char*);
int cplus_fclose(FILE*);
size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
size_t cplus_fread(void*,size_t,size_t,FILE*);
int cplus_fseek(FILE*,long,int);
long cplus_ftell(FILE*);
int cplus_fflush(FILE*);
int dup(int);

int main(void){
    /* tmpfile + setvbuf with a caller-supplied buffer (before any I/O) */
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    static char vbuf[256];
    assert(cplus_setvbuf(f, vbuf, _IOFBF, sizeof vbuf) == 0);

    assert(cplus_fwrite("abcdef", 1, 6, f) == 6);
    assert(cplus_fflush(f) == 0);

    /* fgetpos / fsetpos round-trip */
    fpos_t pos;
    assert(cplus_fgetpos(f, &pos) == 0);
    assert(cplus_fseek(f, 0, SEEK_SET) == 0);
    assert(cplus_ftell(f) == 0);
    assert(cplus_fsetpos(f, &pos) == 0);
    assert(cplus_ftell(f) == 6);

    /* ungetc pushback, then EOF rejection */
    assert(cplus_fseek(f, 0, SEEK_SET) == 0);
    assert(cplus_ungetc('Z', f) == 'Z');
    assert(getc(f) == 'Z');
    assert(cplus_ungetc(EOF, f) == EOF);

    /* fileno on a live stream is a valid descriptor */
    int fd = cplus_fileno(f);
    assert(fd >= 0);

    /* fdopen wrapping a dup'd descriptor */
    int dfd = dup(fd);
    assert(dfd >= 0);
    FILE *g = cplus_fdopen(dfd, "w");
    assert(g != NULL);
    assert(cplus_fileno(g) == dfd);
    assert(cplus_fwrite("xyz", 1, 3, g) == 3);
    assert(cplus_fflush(g) == 0);
    assert(cplus_fclose(g) == 0);

    /* setbuf(stream, NULL) => unbuffered */
    FILE *h = cplus_fopen("/tmp/cplus_stdio3_a.tmp", "wb");
    assert(h != NULL);
    cplus_setbuf(h, NULL);
    assert(cplus_fwrite("q", 1, 1, h) == 1);

    /* freopen reuses the stream, redirecting it to a new path */
    FILE *r = cplus_freopen("/tmp/cplus_stdio3_b.tmp", "w", h);
    assert(r != NULL);
    assert(cplus_fwrite("reopened", 1, 8, r) == 8);
    assert(cplus_fflush(r) == 0);
    assert(cplus_fclose(r) == 0);

    /* verify the unbuffered single byte landed in file a */
    FILE *ra = cplus_fopen("/tmp/cplus_stdio3_a.tmp", "rb");
    assert(ra != NULL);
    char q = 0;
    assert(cplus_fread(&q, 1, 1, ra) == 1);
    assert(q == 'q');
    assert(cplus_fclose(ra) == 0);

    /* verify the reopened file content */
    FILE *rb = cplus_fopen("/tmp/cplus_stdio3_b.tmp", "rb");
    assert(rb != NULL);
    char buf[16] = {0};
    assert(cplus_fread(buf, 1, 8, rb) == 8);
    assert(memcmp(buf, "reopened", 8) == 0);
    assert(cplus_fclose(rb) == 0);

    remove("/tmp/cplus_stdio3_a.tmp");
    remove("/tmp/cplus_stdio3_b.tmp");
    assert(cplus_fclose(f) == 0);
    puts("stdio3: ok");
    return 0;
}

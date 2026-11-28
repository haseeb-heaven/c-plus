/* Covers the platform-backed POSIX/BSD stdlib wrappers added in batch 6
 * (mkstemp / mkdtemp / realpath / BSD qsort_r). Temporary files and
 * directories are created under /tmp and removed again. */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

int cplus_mkstemp(char*);
char *cplus_mkdtemp(char*);
char *cplus_realpath(const char*,char*);
void cplus_qsort_r(void*,size_t,size_t,void*,int(*)(void*,const void*,const void*));

static int cmp_ctx(void *ctx, const void *a, const void *b) {
    int dir = *(const int*)ctx;
    int x = *(const int*)a, y = *(const int*)b;
    return dir * (x - y);
}

int main(void){
    /* mkstemp: returns an open fd and rewrites the trailing XXXXXX */
    { char tmpl[]="/tmp/cplus6_mkstemp_XXXXXX";
      int fd=cplus_mkstemp(tmpl);
      assert(fd>=0);
      assert(strstr(tmpl,"XXXXXX")==NULL);          /* template was replaced */
      assert(access(tmpl,F_OK)==0);                 /* the file now exists */
      assert(write(fd,"hi",2)==2);
      assert(close(fd)==0);
      assert(unlink(tmpl)==0);
      assert(access(tmpl,F_OK)!=0); }

    /* mkdtemp: creates a directory and returns the same pointer */
    { char tmpl[]="/tmp/cplus6_mkdtemp_XXXXXX";
      char *d=cplus_mkdtemp(tmpl);
      assert(d!=NULL);
      assert(strcmp(d,tmpl)==0);
      assert(strstr(tmpl,"XXXXXX")==NULL);
      assert(access(d,F_OK)==0);
      assert(rmdir(d)==0); }

    /* realpath: canonicalises into a caller buffer and into a malloc'd buffer */
    { char buf[PATH_MAX]; char *r=cplus_realpath("/",buf);
      assert(r==buf); assert(strcmp(buf,"/")==0); }
    { char *r=cplus_realpath("/tmp/..",NULL);
      assert(r!=NULL); assert(r[0]=='/');          /* absolute ... */
      assert(strstr(r,"..")==NULL); free(r); }     /* ... and fully canonicalised */

    /* qsort_r: BSD argument order (thunk before comparator), thunk forwarded */
    { int a[6]={5,2,4,1,3,0}; int dir=1;
      cplus_qsort_r(a,6,sizeof a[0],&dir,cmp_ctx);
      for(int i=0;i<6;i++) assert(a[i]==i); }
    { int a[6]={5,2,4,1,3,0}; int dir=-1;
      cplus_qsort_r(a,6,sizeof a[0],&dir,cmp_ctx);
      for(int i=0;i<6;i++) assert(a[i]==5-i); }
    { int a[]={3,1,2}; int dir=1; cplus_qsort_r(a,3,sizeof a[0],&dir,cmp_ctx);
      assert(a[0]==1&&a[1]==2&&a[2]==3); }
    return 0;
}

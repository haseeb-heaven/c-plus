#!/usr/bin/env python3
"""Author the second set of 52 user-provided memory-bug examples."""
import pathlib
OUT = pathlib.Path(__file__).parent / "bugs52b"
OUT.mkdir(exist_ok=True)
P = {}
P[1] = """#include <stdlib.h>
struct Node { int val; struct Node* next; };
int main(void) { struct Node* head=(struct Node*)malloc(sizeof(struct Node)); head->next=(struct Node*)malloc(sizeof(struct Node)); head->next->next=NULL; for (struct Node* curr=head; curr!=NULL; curr=curr->next) { free(curr); } return 0; }"""
P[2] = """#include <stdlib.h>
struct DNode { int val; struct DNode *prev,*next; };
void remove_node(struct DNode* target){ if(target->prev) target->prev->next=target->next; free(target); }
int main(void){ struct DNode*a=(struct DNode*)calloc(1,sizeof(struct DNode)); struct DNode*b=(struct DNode*)calloc(1,sizeof(struct DNode)); a->next=b;b->prev=a; remove_node(a); if(b->prev) b->prev->val=10; free(b); return 0; }"""
P[3] = """#include <stdlib.h>
#include <string.h>
int main(void){ char*buf=(char*)malloc(16); strcpy(buf,"header:payload"); char*payload_ptr=strchr(buf,':')+1; char*new_buf=(char*)realloc(buf,4096); if(new_buf){ buf=new_buf; *payload_ptr='X'; free(buf);} return 0; }"""
P[4] = """#include <pthread.h>
#include <unistd.h>
void*worker(void*arg){ int*v=(int*)arg; usleep(10000); *v=42; return NULL; }
void spawn_job(void){ int local_status=0; pthread_t tid; pthread_create(&tid,NULL,worker,&local_status); pthread_detach(tid); }
int main(void){ spawn_job(); usleep(20000); return 0; }"""
P[5] = """#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
void*cancellable_task(void*arg){(void)arg; void*scratch=malloc(1024); sleep(5); free(scratch); return NULL;}
int main(void){ pthread_t tid; pthread_create(&tid,NULL,cancellable_task,NULL); usleep(1000); pthread_cancel(tid); pthread_join(tid,NULL); return 0; }"""
P[6] = """#include <pthread.h>
#include <stdlib.h>
struct Config { int ready; int port; };
struct Config* global_cfg=NULL; pthread_mutex_t cfg_mtx=PTHREAD_MUTEX_INITIALIZER;
struct Config* get_config(void){ if(global_cfg==NULL){ pthread_mutex_lock(&cfg_mtx); if(global_cfg==NULL){ struct Config*tmp=(struct Config*)malloc(sizeof(struct Config)); tmp->port=8080; tmp->ready=1; global_cfg=tmp;} pthread_mutex_unlock(&cfg_mtx);} return global_cfg; }
int main(void){ struct Config*c=get_config(); free(c); return 0; }"""
P[7] = """#include <pthread.h>
#include <stdlib.h>
int* shared_ptr=NULL;
void*thread_a(void*arg){(void)arg; if(shared_ptr!=NULL){*shared_ptr=10;} return NULL;}
void*thread_b(void*arg){(void)arg; free(shared_ptr); shared_ptr=NULL; return NULL;}
int main(void){ shared_ptr=(int*)malloc(sizeof(int)); pthread_t t1,t2; pthread_create(&t1,NULL,thread_a,NULL); pthread_create(&t2,NULL,thread_b,NULL); pthread_join(t1,NULL); pthread_join(t2,NULL); return 0; }"""
P[8] = """#include <signal.h>
#include <stdlib.h>
void* g_buf=NULL;
void handle_sig(int sig){(void)sig; free(g_buf); g_buf=malloc(64);}
int main(void){ signal(SIGINT,handle_sig); for(int i=0;i<1000;i++){g_buf=malloc(64);free(g_buf);} return 0; }"""
P[9] = """#include <stdio.h>
#include <string.h>
const char* format_id(int id){ static char buf[32]; snprintf(buf,sizeof(buf),"ID-%d",id); return buf; }
int main(void){ const char*a=format_id(1); const char*b=format_id(2); printf("%s vs %s\\n",a,b); return 0; }"""
P[10] = """#include <stdlib.h>
#include <stddef.h>
int main(void){ int*a=(int*)malloc(10*sizeof(int)); int*b=(int*)malloc(10*sizeof(int)); ptrdiff_t diff=b-a; (void)diff; free(a); free(b); return 0; }"""
P[11] = """#include <stdlib.h>
int main(void){ int*a=(int*)malloc(sizeof(int)); int*b=(int*)malloc(sizeof(int)); if(a<b){*a=1;} free(a); free(b); return 0; }"""
P[12] = """#include <stdint.h>
#include <stdlib.h>
int main(void){ int*p=(int*)malloc(sizeof(int)); int*q=(int*)malloc(sizeof(int)); *q=10; uintptr_t ap=(uintptr_t)p; uintptr_t aq=(uintptr_t)q; int*forged=(int*)(ap+(aq-ap)); *forged=20; free(p);free(q); return 0; }"""
P[13] = """#include <stdio.h>
struct Packet { char type; int payload; };
int main(void){ struct Packet pkt; pkt.type=1; pkt.payload=42; FILE*f=fopen("out.bin","wb"); if(f){fwrite(&pkt,sizeof(pkt),1,f);fclose(f);} return 0; }"""
P[14] = """#include <string.h>
struct Header { char flag; int count; };
int main(void){ struct Header h1,h2; h1.flag=1;h1.count=10;h2.flag=1;h2.count=10; int same=memcmp(&h1,&h2,sizeof(struct Header)); return same; }"""
P[15] = """#include <stdio.h>
int main(void){ int*p=NULL; int mode=1; switch(mode){ case 1: { int local_in_case=55; p=&local_in_case; break;} case 2: break;} printf("%d\\n",*p); return 0; }"""
P[16] = """#include <stdio.h>
int main(void){ goto skip_init; int*ptr=NULL; skip_init: if(ptr!=NULL){*ptr=5;} return 0; }"""
P[17] = """#include <alloca.h>
int main(void){ for(int i=0;i<100000;i++){ char*tmp=(char*)alloca(1024); *tmp='A'; } return 0; }"""
P[18] = """int recurse(int n){ char frame[4096]; frame=(char)n; if(n==0) return frame; return recurse(n+1); }
int main(void){ return recurse(1); }"""
P[19] = """#include <stdarg.h>
#include <stdio.h>
void print_vals(int count,...){ va_list args; va_start(args,count); for(int i=0;i<count;i++){ double d=va_arg(args,double); printf("%f\\n",d);} va_end(args);}
int main(void){ print_vals(1,42); return 0; }"""
P[20] = """#include <stdarg.h>
#include <stdlib.h>
void free_all(void*first,...){ va_list args; va_start(args,first); void*p=first; while(p!=NULL){free(p); p=va_arg(args,void*);} va_end(args);}
int main(void){ void*a=malloc(8); void*b=malloc(8); free_all(a,b); return 0; }"""
P[21] = """#include <stdint.h>
#include <stdlib.h>
struct Arena { char*base; size_t offset; };
void*arena_alloc(struct Arena*a,size_t size){ void*ptr=a->base+a->offset; a->offset+=size; return ptr; }
int main(void){ struct Arena a={(char*)malloc(1024),0}; char*c=(char*)arena_alloc(&a,1); *c='A'; double*d=(double*)arena_alloc(&a,sizeof(double)); *d=3.14; free(a.base); return 0; }"""
P[22] = """#include <stdlib.h>
struct RefObj { int refcount; int* payload; };
void release(struct RefObj*obj){ obj->refcount--; if(obj->refcount<=0){free(obj->payload);free(obj);} }
int main(void){ struct RefObj*o=(struct RefObj*)malloc(sizeof(struct RefObj)); o->refcount=1; o->payload=(int*)malloc(sizeof(int)); release(o); release(o); return 0; }"""
P[23] = """#include <stdlib.h>
struct GraphNode { int refs; struct GraphNode* peer; };
void unref(struct GraphNode*n){ if(--n->refs==0){ if(n->peer) unref(n->peer); free(n);} }
int main(void){ struct GraphNode*a=(struct GraphNode*)malloc(sizeof(struct GraphNode)); struct GraphNode*b=(struct GraphNode*)malloc(sizeof(struct GraphNode)); a->refs=2;b->refs=2;a->peer=b;b->peer=a; unref(a); unref(b); return 0; }"""
P[24] = """#include <sys/mman.h>
int main(void){ void*addr=mmap(NULL,0,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(addr!=NULL){*(int*)addr=1;munmap(addr,0);} return 0; }"""
P[25] = """#include <sys/mman.h>
int main(void){ void*addr=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(addr!=MAP_FAILED){munmap(addr,4096);} return 0; }"""
P[26] = """#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void){ int fd=open("tiny.bin",O_RDWR|O_CREAT|O_TRUNC,0600); ftruncate(fd,10); char*map=(char*)mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); close(fd); if(map!=MAP_FAILED){*map='Z';munmap(map,8192);} return 0; }"""
P[27] = """#include <dlfcn.h>
typedef void(*plugin_fn)(void);
int main(void){ void*h=dlopen("./libplugin.so",RTLD_LAZY); if(h){ plugin_fn fn=(plugin_fn)dlsym(h,"run_plugin"); dlclose(h); if(fn) fn(); } return 0; }"""
P[28] = """#include <string.h>
int main(void){ char*literal="a,b,c"; char*tok=strtok(literal,","); (void)tok; return 0; }"""
P[29] = """#include <stdio.h>
int main(void){ char buf; int needed=snprintf(buf,sizeof(buf),"LongStringExceedingBuffer"); buf[needed]='\\0'; return 0; }"""
P[30] = """#include <stdio.h>
#include <string.h>
int main(void){ char buf; FILE*f=fopen("data.txt","rb"); if(f){ size_t n=fread(buf,1,sizeof(buf),f); fclose(f); if(strstr(buf,"key")){return 1;} (void)n; } return 0; }"""
P[31] = """#include <stdlib.h>
#include <string.h>
int bad_cmp(const void*a,const void*b){ return strcmp((const char*)a,(const char*)b); }
int main(void){ const char*arr[]={"banana","apple","cherry"}; qsort(arr,3,sizeof(const char*),bad_cmp); return 0; }"""
P[32] = """#include <stdint.h>
void write_64(uint64_t*out){ *out=0x1122334455667788ULL; }
int main(void){ uint32_t small=0; write_64((uint64_t*)&small); return (int)small; }"""
P[33] = """#include <stddef.h>
void add_arrays(int*restrict dst,const int*restrict src,size_t n){ for(size_t i=0;i<n;i++) dst[i]+=src[i]; }
int main(void){ int data={1,2,3,4,5,6}; add_arrays(data+1,data,4); return data; }"""
P[34] = """#include <stdlib.h>
#include <string.h>
struct Blob { size_t len; char bytes[]; };
int main(void){ struct Blob*b1=(struct Blob*)malloc(sizeof(struct Blob)+10); b1->len=10; memset(b1->bytes,'A',10); struct Blob b2=*b1; (void)b2; free(b1); return 0; }"""
P[35] = """#include <stddef.h>
#define container_of(ptr,type,member) ((type*)((char*)(ptr)-offsetof(type,member)))
struct Item { int id; int node; };
int main(void){ int*member_ptr=NULL; struct Item*item=container_of(member_ptr,struct Item,node); item->id=1; return 0; }"""
P[36] = """#include <stdlib.h>
void consume_and_free(int**p){ free(*p); }
int main(void){ int*buf=(int*)malloc(sizeof(int)); *buf=7; consume_and_free(&buf); if(buf!=NULL){*buf=8;} return 0; }"""
P[37] = """#include <stdlib.h>
void alloc_buffer(char**out){ *out=(char*)malloc(64); }
int main(void){ char*p=NULL; alloc_buffer(&p); alloc_buffer(&p); free(p); return 0; }"""
P[38] = """#include <stdio.h>
#include <string.h>
const char* cache_key=NULL;
void put_cache(const char*key){ cache_key=key; }
void populate(void){ char temp_key; strcpy(temp_key,"session_123"); put_cache(temp_key); }
int main(void){ populate(); printf("%s\\n",cache_key); return 0; }"""
P[39] = """#include <stdlib.h>
typedef void(*cb_t)(void*ctx);
cb_t global_cb=NULL; void* global_ctx=NULL;
void register_cb(cb_t cb,void*ctx){global_cb=cb;global_ctx=ctx;}
void on_event(void*ctx){ *(int*)ctx+=1; }
int main(void){ int*state=(int*)malloc(sizeof(int)); *state=0; register_cb(on_event,state); free(state); if(global_cb) global_cb(global_ctx); return 0; }"""
P[40] = """#include <stdlib.h>
int main(void){ size_t cap=8; int*ring=(int*)malloc(cap*sizeof(int)); size_t head=7; head=(head+1)%(cap+1); ring[head]=99; free(ring); return 0; }"""
P[41] = """#include <stdint.h>
#include <stdlib.h>
int main(void){ int*p=(int*)malloc(sizeof(int)); int*tagged=(int*)((uintptr_t)p|1ULL); *tagged=42; free((void*)((uintptr_t)tagged&~1ULL)); return 0; }"""
P[42] = """#include <stdint.h>
#include <stdlib.h>
int main(void){ int*p=(int*)malloc(sizeof(int)); int*tagged=(int*)((uintptr_t)p|1ULL); free(tagged); return 0; }"""
P[43] = """#include <stdlib.h>
struct Tree { struct Tree *left,*right; };
void destroy_tree(struct Tree*root){ if(!root)return; destroy_tree(root->left); destroy_tree(root->right); free(root); }
int main(void){ struct Tree*root=NULL; for(int i=0;i<500000;i++){ struct Tree*n=(struct Tree*)calloc(1,sizeof(struct Tree)); n->left=root; root=n;} destroy_tree(root); return 0; }"""
P[44] = """#include <stdlib.h>
struct Entry { char* name; };
int main(void){ struct Entry*list=(struct Entry*)malloc(5*sizeof(struct Entry)); for(int i=0;i<5;i++){ if(i==3){ for(int j=0;j<5;j++){free(list[j].name);} free(list); return 1;} list[i].name=(char*)malloc(16);} return 0; }"""
P[45] = """#include <stdlib.h>
struct FreeSlot { struct FreeSlot* next; };
struct FreeSlot* free_list=NULL;
void pool_free(void*ptr){ struct FreeSlot*slot=(struct FreeSlot*)ptr; slot->next=free_list; free_list=slot; }
int main(void){ void*block=malloc(32); pool_free(block); pool_free(block); free(block); return 0; }"""
P[46] = """#include <stdlib.h>
#include <string.h>
struct Item { int* data; };
int main(void){ struct Item*arr=(struct Item*)malloc(3*sizeof(struct Item)); arr[0].data=(int*)malloc(sizeof(int)); arr[1].data=(int*)malloc(sizeof(int)); arr[2].data=(int*)malloc(sizeof(int)); free(arr[0].data); memmove(&arr[0],&arr[1],2*sizeof(struct Item)); for(int i=0;i<3;i++){free(arr[i].data);} free(arr); return 0; }"""
P[47] = """#include <aio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
void start_async_read(int fd){ char local_buf; struct aiocb cb; memset(&cb,0,sizeof(cb)); cb.aio_fildes=fd; cb.aio_buf=local_buf; cb.aio_nbytes=sizeof(local_buf); aio_read(&cb); }
int main(void){ int fd=open("data.txt",O_RDONLY); if(fd>=0){start_async_read(fd);close(fd);} return 0; }"""
P[48] = """#include <stdio.h>
int main(void){ int valid_num=123; printf("%s\\n",valid_num); return 0; }"""
P[49] = """#include <stdio.h>
#include <stdlib.h>
int main(void){ setenv("MY_VAR","initial_value",1); char*val=getenv("MY_VAR"); for(int i=0;i<100;i++){setenv("OTHER_VAR","growing_environment_forces_realloc",1);} printf("%s\\n",val); return 0; }"""
P[50] = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void set_local_env(void){ char env_str; strcpy(env_str,"APP_MODE=prod"); putenv(env_str); }
int main(void){ set_local_env(); printf("%s\\n",getenv("APP_MODE")); return 0; }"""
P[51] = """#include <stdlib.h>
static void auto_free(void*p){ free(*(void**)p); }
#define AUTO_FREE __attribute__((cleanup(auto_free)))
int main(void){ AUTO_FREE int*ptr; if(1) return 0; ptr=(int*)malloc(sizeof(int)); return 0; }"""
P[52] = """#include <stdlib.h>
static void auto_free(void*p){ free(*(void**)p); }
#define AUTO_FREE __attribute__((cleanup(auto_free)))
int* make_int(void){ AUTO_FREE int*p=(int*)malloc(sizeof(int)); *p=42; return p; }
int main(void){ int*val=make_int(); *val=10; return 0; }"""

for n, body in P.items():
    (OUT / f"bug_{n:02d}.c").write_text(body + "\n")
print(f"wrote {len(P)} examples")

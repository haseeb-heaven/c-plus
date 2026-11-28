#!/usr/bin/env python3
"""25 format-string / stdio scenarios from the review. Each expects reject."""
import pathlib, subprocess, sys, os
ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/fmt25"
OUT.mkdir(parents=True, exist_ok=True)

H = "#include <stdio.h>\n"
CASES = {
1: H+"int main(void){char in[64];if(fgets(in,sizeof(in),stdin))printf(in);return 0;}",
2: H+"int main(void){char in[64];if(fgets(in,sizeof(in),stdin))fprintf(stderr,in);return 0;}",
3: H+"int main(void){char in[64],out[128];if(fgets(in,sizeof(in),stdin))snprintf(out,sizeof(out),in);return 0;}",
4: "#include <stdint.h>\n"+H+"int main(void){uintptr_t f=0xdeadbeefULL;printf(\"Count: %n\\n\",f);return 0;}",
5: H+"int main(void){printf(\"ID=%d Name=%s\\n\",42);return 0;}",
6: H+"int main(void){int c=0x12345678;printf(\"Code: %s\\n\",c);return 0;}",
7: H+"int main(void){const char*n=\"admin\";int w=16;printf(\"%*s\\n\",n,w);return 0;}",
8: H+"int main(void){const char*f=\"%999$s\";printf(f,\"hello\");return 0;}",
9: H+"int main(void){char t[4]={'A','B','C','D'};printf(\"Tag: %s\\n\",t);return 0;}",
10: H+"int main(void){char in[64];if(fgets(in,sizeof(in),stdin)){int(*fn)(const char*,...)=printf;fn(in);}return 0;}",
11: H+"void read_line(char*buf){fgets(buf,sizeof(buf),stdin);}\nint main(void){char line[64];read_line(line);return 0;}",
12: H+"int main(void){char b[32];fgets(b,sizeof(b),stdin);printf(\"Got: %s\\n\",b);return 0;}",
13: H+"int main(void){char s[8];fgets(s,64,stdin);return 0;}",
14: H+"int main(void){char b[32];fgets(b,sizeof(b),NULL);return 0;}",
15: "#include <stddef.h>\n"+H+"int main(void){char b[64];size_t h=(size_t)-1/4+10;size_t n=fread(b,4,h,stdin);return (int)n;}",
16: H+"int main(void){char b[8];size_t n=fread(b,1,64,stdin);return (int)n;}",
17: H+"int main(void){int r[4];size_t n=fread(r,sizeof(r),1,stdin);if(n==0)return r[0];return 0;}",
18: H+"int main(void){FILE*f=fopen(\"/nonexistent_dir/file.txt\",\"r\");fprintf(f,\"data\\n\");fclose(f);return 0;}",
19: H+"int process(int a){FILE*f=fopen(\"t\",\"w\");if(!f)return -1;if(a)return -1;fclose(f);return 0;}\nint main(void){return process(1);}",
20: H+"int main(void){FILE*f=fopen(\"t\",\"w\");if(f){FILE*f2=f;fclose(f);f=NULL;fprintf(f2,\"use after close\\n\");}return 0;}",
21: H+"int main(void){char w[8];int rc=fscanf(stdin,\"%s\",w);return rc;}",
22: H+"int main(void){int n=0;int rc=sscanf(\"1234\",\"%d\",n);return rc;}",
23: "#define _POSIX_C_SOURCE 200809L\n"+H+"#include <stdlib.h>\nint main(void){setenv(\"K\",\"i\",1);const char*p=getenv(\"K\");for(int i=0;i<100;i++)setenv(\"F\",\"x\",1);printf(\"K=%s\\n\",p);return 0;}",
24: "#include <errno.h>\n#include <string.h>\n"+H+"int main(void){const char*a=strerror(ENOENT);const char*b=strerror(ENOMEM);printf(\"%s | %s\\n\",a,b);return 0;}",
25: H+"int main(void){char b[8];int n=snprintf(b,sizeof(b),\"This string exceeds 8 bytes\");b[n]='\\0';return 0;}",
}
res=[]
for n, body in CASES.items():
    p=OUT/f"fmt{n:02d}.c"; p.write_text(body+"\n")
    r=subprocess.run([CC,"check","--strict",str(p)],capture_output=True,text=True)
    diag=r.stderr+r.stdout
    res.append((n, r.returncode!=0, [l for l in diag.splitlines() if "error:" in l or "warning:" in l][:1]))
det=sum(1 for _,d,_ in res if d)
print(f"fmt25: {len(CASES)} scenarios, {det} rejected in STRICT")
for n,d,diag in res:
    print(f"  {n:2} {'REJECT' if d else 'ACCEPT':6} {diag[0][:100] if diag else ''}")

#!/usr/bin/env python3
"""64-bit runtime arithmetic regression (issues #64-#66)."""
import datetime, json, os, platform, subprocess, sys
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.join(HERE,'results')
os.makedirs(OUT,exist_ok=True)
CC=os.environ.get('CC','/usr/bin/clang')
LIB=os.path.join(ROOT,'build','libcplus_runtime.a')
SRC='''#include <stdio.h>
#include <stdint.h>
extern int64_t __cplus_checked_div_i64(int64_t,int64_t,unsigned,unsigned);
extern int64_t __cplus_checked_rem_i64(int64_t,int64_t,unsigned,unsigned);
extern int64_t __cplus_checked_shr_i64(int64_t,int64_t,unsigned,unsigned);
extern int64_t __cplus_checked_shl_i64(int64_t,int64_t,unsigned,unsigned);
extern int64_t __cplus_checked_narrow_i64(uint64_t,unsigned,unsigned,unsigned,unsigned);
int main(void){
 printf("div=%lld rem=%lld shr=%lld shl=%lld narrow=%lld\\n",
  (long long)__cplus_checked_div_i64((int64_t)UINT64_MAX,2,64,0),
  (long long)__cplus_checked_rem_i64((int64_t)UINT64_MAX,2,64,0),
  (long long)__cplus_checked_shr_i64((int64_t)UINT64_MAX,1,64,0),
  (long long)__cplus_checked_shl_i64(1,2,64,1),
  (long long)__cplus_checked_narrow_i64((uint64_t)(int64_t)-1,64,1,32,1));
 printf("u0=%lld u65=%lld s0=%lld\\n",
  (long long)__cplus_checked_shr_i64(-1,1,0,0),
  (long long)__cplus_checked_shr_i64(-1,1,65,0),
  (long long)__cplus_checked_shr_i64(-1,1,0,1));
 return 0;
}
'''
EXPECT=('div=9223372036854775807 rem=1 shr=9223372036854775807 shl=4 narrow=-1\n'
        'u0=9223372036854775807 u65=9223372036854775807 s0=-1\n')
def run(argv,t=60):
 try:
  p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=t)
  return {'command':argv,'rc':p.returncode,'stdout':p.stdout.decode(errors='replace'),'stderr':p.stderr.decode(errors='replace'),'timed_out':False}
 except subprocess.TimeoutExpired: return {'command':argv,'rc':None,'stdout':'','stderr':'','timed_out':True}
def main():
 p=os.path.join(HERE,'rt64.c'); open(p,'w').write(SRC)
 exe=os.path.join(OUT,'rt64.bin')
 b=run([CC,'-std=c11',p,LIB,'-o',exe])
 steps=[b]; ok=b['rc']==0
 if ok:
  r=run([exe]); steps.append(r); ok=r['rc']==0 and r['stdout']==EXPECT
 row={'case':'runtime_64bit_arithmetic','status':'PASS' if ok else 'FAIL','expected':EXPECT,'steps':steps}
 payload={'generated':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'cc':CC,'results':[row],'failures':[] if ok else ['runtime_64bit_arithmetic']}
 json.dump(payload,open(os.path.join(OUT,'summary.json'),'w'),indent=2)
 with open(os.path.join(OUT,'summary.txt'),'w') as f:
  f.write('64-bit runtime arithmetic\n%s\n%s\n\n'%(payload['host'],CC))
  for s in steps: f.write('cmd: %s\nrc=%s out=%r err=%r\n'%(' '.join(s['command']),s['rc'],s['stdout'],s['stderr'][:300]))
  f.write('\n%s\n'%row['status'])
 print('runtime_arith_64:',row['status'])
 return 0 if ok else 1
if __name__=='__main__': sys.exit(main())

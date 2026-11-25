#!/usr/bin/env python3
"""Nested method-call rewriting regression (issue #46)."""
import datetime, json, os, platform, subprocess, sys
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
BIN=os.environ.get('CPLUS_BIN',os.path.join(ROOT,'build','c+'))
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.join(HERE,'results')
os.makedirs(OUT,exist_ok=True)
POS={
 'struct_member':'''#include <stdio.h>
class Counter { int val; void inc(void){ this->val++; } int get(void){ return this->val; } };
struct Wrapper { Counter c; };
int main(void){ struct Wrapper w; w.c.val=0; w.c.inc(); printf("%d\\n", w.c.get()); return 0; }
''',
 'class_member':'''#include <stdio.h>
class Counter { int val; void inc(void){ this->val++; } int get(void){ return this->val; } };
class Outer { Counter c; void bump(void){ this->c.inc(); } int get(void){ return this->c.get(); } };
int main(void){ Outer o; o.c.val=0; o.bump(); printf("%d\\n", o.get()); return 0; }
'''}
def run(argv,t=60):
 try:
  p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=t)
  return {'command':argv,'rc':p.returncode,'stdout':p.stdout.decode(errors='replace'),'stderr':p.stderr.decode(errors='replace'),'timed_out':False}
 except subprocess.TimeoutExpired: return {'command':argv,'rc':None,'stdout':'','stderr':'','timed_out':True}
def main():
 rows,fails=[],[]
 for name,src in POS.items():
  p=os.path.join(HERE,name+'.cp'); open(p,'w').write(src)
  exe=os.path.join(OUT,name+'.bin')
  b=run([BIN,'build','--strict','--sanitize=none',p,'-o',exe]); steps=[b]
  ok=b['rc']==0 and os.path.exists(exe)
  if ok:
   r=run([exe]); steps.append(r); ok=r['rc']==0 and r['stdout']=='1\n'
  rows.append({'case':name,'status':'PASS' if ok else 'FAIL','expected':'1','steps':steps})
  if not ok: fails.append(name)
  print('%-18s %s'%(name,'PASS' if ok else 'FAIL'))
 payload={'generated':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'compiler':BIN,'results':rows,'failures':fails}
 json.dump(payload,open(os.path.join(OUT,'summary.json'),'w'),indent=2)
 open(os.path.join(OUT,'summary.txt'),'w').write('Nested method suite\n%s\n%s\n\n'%(payload['host'],BIN)+''.join('%s %s\n'%(r['case'],r['status']) for r in rows)+'\ncases=%d passed=%d failed=%d\n'%(len(rows),len(rows)-len(fails),len(fails)))
 print('nested_method: %d cases, %d failures'%(len(rows),len(fails)))
 return 1 if fails else 0
if __name__=='__main__': sys.exit(main())

#!/usr/bin/env python3
"""By-value RAII ownership transfer regression (issue #48)."""
import datetime, json, os, platform, subprocess, sys
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
BIN=os.environ.get('CPLUS_BIN',os.path.join(ROOT,'build','c+'))
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.join(HERE,'results')
os.makedirs(OUT,exist_ok=True)
PRE='''#include <stdio.h>
#include <stdlib.h>
class Resource {
  int *ptr;
  Resource(void){ this->ptr=(int*)malloc(sizeof(int)); if(this->ptr)*this->ptr=42; }
  void drop(void){ if(this->ptr){ printf("DROP\\n"); free(this->ptr); this->ptr=0; } }
};
'''
CASES={
 'by_value_transfer': PRE+'static void consume(Resource r){ printf("CONSUME %d\\n", *r.ptr); }\nint main(void){ Resource r = Resource(); consume(r); return 0; }\n',
 'by_reference_control': PRE+'static void peek(Resource *r){ printf("PEEK %d\\n", *r->ptr); }\nint main(void){ Resource r = Resource(); peek(&r); return 0; }\n',
}
def run(argv,t=60):
 try:
  p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=t)
  return {'command':argv,'rc':p.returncode,'stdout':p.stdout.decode(errors='replace'),'stderr':p.stderr.decode(errors='replace'),'timed_out':False}
 except subprocess.TimeoutExpired: return {'command':argv,'rc':None,'stdout':'','stderr':'','timed_out':True}
def main():
 rows,fails=[],[]
 for name,src in CASES.items():
  p=os.path.join(HERE,name+'.cp'); open(p,'w').write(src)
  exe=os.path.join(OUT,name+'.bin')
  b=run([BIN,'build','--strict','--sanitize=none',p,'-o',exe]); steps=[b]
  ok=b['rc']==0 and os.path.exists(exe)
  if ok:
   r=run([exe]); steps.append(r)
   ok=r['rc']==0 and r['stdout'].count('DROP')==1 and ('CONSUME 42' in r['stdout'] or 'PEEK 42' in r['stdout'])
  rows.append({'case':name,'status':'PASS' if ok else 'FAIL','steps':steps})
  if not ok: fails.append(name)
  print('%-22s %s'%(name,'PASS' if ok else 'FAIL'))
 payload={'generated':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'compiler':BIN,'results':rows,'failures':fails}
 json.dump(payload,open(os.path.join(OUT,'summary.json'),'w'),indent=2)
 open(os.path.join(OUT,'summary.txt'),'w').write('RAII transfer suite\n%s\n%s\n\n'%(payload['host'],BIN)+''.join('%s %s\n'%(r['case'],r['status']) for r in rows)+'\ncases=%d passed=%d failed=%d\n'%(len(rows),len(rows)-len(fails),len(fails)))
 print('raii_transfer: %d cases, %d failures'%(len(rows),len(fails)))
 return 1 if fails else 0
if __name__=='__main__': sys.exit(main())

#!/usr/bin/env python3
"""Function-pointer throw alias regression (issue #47, fnptr portion)."""
import datetime, json, os, platform, subprocess, sys
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
BIN=os.environ.get('CPLUS_BIN',os.path.join(ROOT,'build','c+'))
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.join(HERE,'results')
os.makedirs(OUT,exist_ok=True)
PRE='#include <stdio.h>\nresource struct R { int id; };\nvoid R_drop(R *r){ if(r->id){ printf("DROP\\n"); r->id=0; } }\n'
CASES={
 'neg_fnptr_throw': PRE+'static void boom(void){ throw 1; }\nint main(void){ void (*fp)(void)=boom; try { R r={1}; fp(); } catch (Exception e) { printf("CAUGHT\\n"); } return 0; }\n',
 'pos_fnptr_normal': PRE+'static void normal(void){ printf("NORMAL\\n"); }\nint main(void){ void (*fp)(void)=normal; try { R r={1}; fp(); } catch (Exception e) { printf("CAUGHT\\n"); } return 0; }\n',
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
  c=run([BIN,'check','--strict','--sanitize=none',p])
  ok=(c['rc'] not in (0,None) and 'may throw' in c['stderr']) if name.startswith('neg') else c['rc']==0
  rows.append({'case':name,'status':'PASS' if ok else 'FAIL','steps':[c]})
  if not ok: fails.append(name)
  print('%-22s %s'%(name,'PASS' if ok else 'FAIL'))
 payload={'generated':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'compiler':BIN,'results':rows,'failures':fails}
 json.dump(payload,open(os.path.join(OUT,'summary.json'),'w'),indent=2)
 open(os.path.join(OUT,'summary.txt'),'w').write('Throw alias suite\n%s\n%s\n\n'%(payload['host'],BIN)+''.join('%s %s\n'%(r['case'],r['status']) for r in rows)+'\ncases=%d passed=%d failed=%d\n'%(len(rows),len(rows)-len(fails),len(fails)))
 print('throw_alias: %d cases, %d failures'%(len(rows),len(fails)))
 return 1 if fails else 0
if __name__=='__main__': sys.exit(main())

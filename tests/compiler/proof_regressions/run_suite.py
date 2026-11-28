#!/usr/bin/env python3
"""Regression suite for Agent 1 proofs #38/#39/#40."""
import datetime, json, os, platform, subprocess, sys
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
BIN=os.environ.get('CPLUS_BIN',os.path.join(ROOT,'build','c+'))
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.join(HERE,'results')
os.makedirs(OUT,exist_ok=True)
NEG=[
 ('array_alias_uaf','tests/compiler/safety_breakers/proof_agent1_array_alias_uaf.c','use-after-free'),
 ('struct_downcast','tests/compiler/safety_breakers/proof_agent1_struct_downcast_oob.c','exceeds heap allocation'),
 ('ptr_arith_underflow','tests/compiler/safety_breakers/proof_agent1_ptr_arith_underflow.c','unproven runtime offset'),
]
POS={
 'array_alias_ok':'#include <stdlib.h>\nint main(void){ int *p[2]; p[0]=(int*)malloc(sizeof(int)); if(!p[0])return 1; *p[0]=1; int *a=p[0]; *a=2; free(p[0]); return 0; }\n',
 'struct_layout_ok':'#include <stdlib.h>\nstruct S{int a[4];};\nint main(void){ struct S*s=(struct S*)malloc(sizeof *s); if(!s)return 1; s->a[3]=1; free(s); return 0; }\n',
 'ptr_arith_const_ok':'#include <stdio.h>\nint main(void){ int a[4]={1,2,3,4}; int *p=a+2; printf("%d\\n",*p); return 0; }\n',
}
def run(argv,t=60):
 try:
  p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=t)
  return {'command':argv,'rc':p.returncode,'stdout':p.stdout.decode(errors='replace'),'stderr':p.stderr.decode(errors='replace'),'timed_out':False}
 except subprocess.TimeoutExpired: return {'command':argv,'rc':None,'stdout':'','stderr':'','timed_out':True}
def main():
 rows,fails=[],[]
 for name,rel,diag in NEG:
  c=run([BIN,'check','--strict','--sanitize=none',os.path.join(ROOT,rel)])
  ok=c['rc'] not in (0,None) and diag in c['stderr']
  rows.append({'case':name,'status':'PASS' if ok else 'FAIL','expected':diag,'steps':[c]})
  if not ok: fails.append(name)
  print('%-22s %s'%(name,'PASS' if ok else 'FAIL'))
 for name,src in POS.items():
  p=os.path.join(HERE,name+'.c'); open(p,'w').write(src)
  c=run([BIN,'check','--strict','--sanitize=none',p])
  ok=c['rc']==0
  rows.append({'case':name,'status':'PASS' if ok else 'FAIL','expected':'accept','steps':[c]})
  if not ok: fails.append(name)
  print('%-22s %s'%(name,'PASS' if ok else 'FAIL'))
 payload={'generated':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'compiler':BIN,'results':rows,'failures':fails}
 json.dump(payload,open(os.path.join(OUT,'summary.json'),'w'),indent=2)
 open(os.path.join(OUT,'summary.txt'),'w').write('Proof regressions\n%s\n%s\n\n'%(payload['host'],BIN)+''.join('%s %s\n'%(r['case'],r['status']) for r in rows)+'\ncases=%d passed=%d failed=%d\n'%(len(rows),len(rows)-len(fails),len(fails)))
 print('proof_regressions: %d cases, %d failures'%(len(rows),len(fails)))
 return 1 if fails else 0
if __name__=='__main__': sys.exit(main())
